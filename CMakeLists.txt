cmake_minimum_required(VERSION 3.16)
# NOTE: keep the VERSION argument below in sync with:
#   - the root `QUICX_VERSION` and `VERSION.txt` files
#   - the QUICX_VERSION_MAJOR/MINOR/PATCH macros in
#     include/quicx/common/version.h
#   - the `version = "..."` field in MODULE.bazel
#   - the version badges in README.md / README_cn.md
project(QuicX VERSION 1.0.0 LANGUAGES C CXX)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Output path
set(EXECUTABLE_OUTPUT_PATH ${PROJECT_BINARY_DIR}/bin)
set(LIBRARY_OUTPUT_PATH ${PROJECT_BINARY_DIR}/lib)

# Use submodule boringssl if present
set(BORINGSSL_ROOT ${PROJECT_SOURCE_DIR}/third/boringssl)
if(EXISTS ${BORINGSSL_ROOT}/CMakeLists.txt)
    add_subdirectory(${BORINGSSL_ROOT} build_boringssl EXCLUDE_FROM_ALL)
    set(boringssl_SOURCE_DIR ${BORINGSSL_ROOT})

    # BoringSSL is third-party code; we never patch it. Newer GCC (12+) plus
    # `-fsanitize=address` causes spurious `-Wmaybe-uninitialized` diagnostics
    # in BoringSSL's fipsmodule, and BoringSSL itself uses `-Werror`. Demote
    # warnings to non-fatal for BoringSSL targets only, so sanitizer / coverage
    # builds keep compiling without touching upstream sources.
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        foreach(_bssl_tgt crypto ssl decrepit fipsmodule bssl)
            if(TARGET ${_bssl_tgt})
                target_compile_options(${_bssl_tgt} PRIVATE -Wno-error)
            endif()
        endforeach()
    endif()
else()
    message(FATAL_ERROR "third/boringssl submodule missing. Please init submodules or disable build.")
endif()

# Threads
find_package(Threads REQUIRED)

# Ensure BoringSSL headers are used instead of system OpenSSL
if(WIN32)
    # On Windows, explicitly set include paths to avoid system OpenSSL
    include_directories(SYSTEM ${boringssl_SOURCE_DIR}/include)
    add_definitions(-DOPENSSL_NO_SHARED)
    
    # MSVC-specific compiler flags
    if(MSVC)
        add_compile_options(/wd4996 /wd4267 /wd4244 /utf-8 /MP)
        add_definitions(-D_CRT_SECURE_NO_WARNINGS)
        add_definitions(-DWIN32_LEAN_AND_MEAN)
        add_definitions(-DNOMINMAX)
    endif()
endif()

# Source collection
# -----------------
# CMake's official guidance discourages plain `file(GLOB_RECURSE ...)` for
# generating source lists: CMake doesn't re-run when a file is added or
# removed, so IDE/CI build trees go stale until someone re-runs cmake by
# hand. Two acceptable fixes:
#   1. List sources explicitly (most robust, but ~280 files here).
#   2. Pass CONFIGURE_DEPENDS, which makes the build tool stat the globbed
#      directories on every build and re-run CMake when the contents change
#      (CMake 3.12+; we already require 3.16 above).
#
# We take option 2 deliberately:
#   - the Bazel build (BUILD.bazel) is also glob-based, and Bazel re-evaluates
#     globs on every invocation, so it already has correct staleness behavior
#     on that side; matching the *shape* of source selection across the two
#     systems keeps maintenance cost down.
#   - keeping a parallel hand-maintained source list in CMake would diverge
#     from Bazel's glob over time with zero compensating benefit.
#
# Note on platform-specific files: src/common contains os/{posix,win}/ and
# network/{linux,macos,windows}/ trees. These are guarded internally by
# `#ifdef __APPLE__` / `#ifdef _WIN32` / `#ifdef WIN32`, so on the wrong host
# they compile to empty objects rather than producing build errors. Bazel
# selects the right subset via `select()`; CMake currently relies on the
# guards, which costs a small amount of compile time but is functionally
# correct. Tightening this is tracked separately and is independent of the
# CONFIGURE_DEPENDS fix.
function(collect_sources dir output_var)
    file(GLOB_RECURSE new_sources
        CONFIGURE_DEPENDS
        LIST_DIRECTORIES false
        "${dir}/*.cpp"
        "${dir}/*.c"
        "${dir}/*.h"
        "${dir}/*.hpp"
    )
    set(${output_var} ${new_sources} PARENT_SCOPE)
endfunction()

collect_sources(${PROJECT_SOURCE_DIR}/src/common common_files)
collect_sources(${PROJECT_SOURCE_DIR}/src/quic quic_files)
collect_sources(${PROJECT_SOURCE_DIR}/src/http3 http3_files)
collect_sources(${PROJECT_SOURCE_DIR}/src/upgrade upgrade_files)

set(common_source ${common_files})
set(quic_source ${quic_files})
set(http3_source ${http3_files})
set(upgrade_source ${upgrade_files})

# Two static libraries:
#   - quicx: QUIC transport (common + quic)
#   - http3: HTTP/3 + h2-upgrade application stack, layered on top of quicx
#
# http3 deliberately does NOT re-compile common/quic sources. It contains
# only the http3 + upgrade objects, and pulls in quicx via a PUBLIC link
# dependency. This:
#   - cuts the second archive's compile time roughly in half;
#   - keeps each QUIC symbol in exactly one archive (libquicx.a), avoiding
#     duplicate-symbol surprises if a downstream were to link both.
# Downstream consumers that previously linked just `http3` keep working
# unchanged: PUBLIC propagation makes `target_link_libraries(... http3)`
# transitively pull in quicx (and through it Threads/crypto/ssl/filesystem).
add_library(quicx STATIC ${common_source} ${quic_source})
add_library(http3 STATIC ${http3_source} ${upgrade_source})

set_target_properties(quicx PROPERTIES POSITION_INDEPENDENT_CODE ON)
set_target_properties(http3 PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_include_directories(quicx
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
        # BoringSSL headers are exposed through internal headers under src/
        # (e.g. quic/crypto/type.h uses EVP_AEAD_CTX). Targets that compile
        # against the in-tree src/ tree (benchmarks, fuzzers, internal tests)
        # need this on their include path. Only exposed in the BUILD tree;
        # installed consumers don't see src/ headers, so they don't need it.
        $<BUILD_INTERFACE:${boringssl_SOURCE_DIR}/include>
    PRIVATE
        ${PROJECT_SOURCE_DIR}/src
        ${boringssl_SOURCE_DIR}/include
)
# http3's own translation units need the same private include layout as quicx
# (src/ root and BoringSSL headers). Public include dirs come transitively
# from the quicx link below.
target_include_directories(http3
    PRIVATE
        ${PROJECT_SOURCE_DIR}/src
        ${boringssl_SOURCE_DIR}/include
)

# Check if we need to link filesystem library (required on some Linux distributions)
if(UNIX AND NOT APPLE)
    find_library(STDCXXFS_LIBRARY stdc++fs)
    if(STDCXXFS_LIBRARY)
        set(FILESYSTEM_LIBRARY ${STDCXXFS_LIBRARY})
    endif()
endif()

target_link_libraries(quicx
    PUBLIC
        Threads::Threads
        ${FILESYSTEM_LIBRARY}
    PRIVATE
        crypto
        ssl
)
# http3 layers on quicx. PUBLIC so that:
#   1. http3's own .cpp files see quicx's PUBLIC include dirs at compile time;
#   2. consumers linking only `http3` (or the exported `quicx::http3`) get
#      quicx + its transitive deps (Threads, filesystem, BoringSSL crypto/ssl)
#      pulled in automatically by the linker.
target_link_libraries(http3
    PUBLIC
        quicx
)

# Public headers live under ${PROJECT_SOURCE_DIR}/include/quicx/.
# They are exposed via target_include_directories(... PUBLIC) above.
# (Install rules are defined separately, see install/quicxConfig.cmake handling.)

# ---------------------------------------------------------------------------
# Sanitizer support
# Usage: cmake -DSANITIZER=asan|ubsan|tsan ...
# ---------------------------------------------------------------------------
set(SANITIZER "" CACHE STRING "Enable a sanitizer: asan, ubsan, tsan, or empty to disable")
set_property(CACHE SANITIZER PROPERTY STRINGS "" "asan" "ubsan" "tsan")

if(SANITIZER)
    message(STATUS "Sanitizer: ${SANITIZER}")
    if(SANITIZER STREQUAL "asan")
        set(SANITIZER_FLAGS "-fsanitize=address -fno-omit-frame-pointer -O1 -g")
    elseif(SANITIZER STREQUAL "ubsan")
        set(SANITIZER_FLAGS "-fsanitize=undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -O1 -g")
    elseif(SANITIZER STREQUAL "tsan")
        set(SANITIZER_FLAGS "-fsanitize=thread -fno-omit-frame-pointer -O1 -g")
    else()
        message(FATAL_ERROR "Unknown SANITIZER value: ${SANITIZER}. Use asan, ubsan, or tsan.")
    endif()

    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${SANITIZER_FLAGS}")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${SANITIZER_FLAGS}")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${SANITIZER_FLAGS}")
    set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} ${SANITIZER_FLAGS}")
else()
    message(STATUS "Sanitizer: DISABLED")
endif()

# qlog support
option(QUICX_ENABLE_QLOG "Enable qlog support" ON)
if(QUICX_ENABLE_QLOG)
    add_definitions(-DQUICX_ENABLE_QLOG)
    message(STATUS "qlog support: ENABLED")
else()
    message(STATUS "qlog support: DISABLED")
endif()

# Build examples
option(BUILD_EXAMPLES "Build examples" ON)
if(BUILD_EXAMPLES)
    add_subdirectory(example)
endif()

# Define an option to enable or disable testing
option(ENABLE_TESTING "Enable building of unit tests" ON)

# Enable testing if the option is set
if(ENABLE_TESTING)
    add_subdirectory(test/unit_test)
endif()

# Benchmarks
option(ENABLE_BENCHMARKS "Enable building benchmarks" ON)
if(ENABLE_BENCHMARKS)
    add_subdirectory(test/benchmarks)
endif()

# Fuzzing targets (libFuzzer)
option(ENABLE_FUZZING "Enable building fuzz targets" OFF)
if(ENABLE_FUZZING)
    add_subdirectory(test/fuzz)
endif()

# Congestion control simulator tests
option(ENABLE_CC_SIMULATOR "Enable building congestion control simulator tests" ON)
if(ENABLE_CC_SIMULATOR)
    add_subdirectory(test/congestion_control)
endif()

# integration tests
option(ENABLE_INTEGRATION "Enable building integration tests" ON)
if(ENABLE_INTEGRATION)
    add_subdirectory(test/integration)
endif()

# interop tests
option(ENABLE_INTEROP "Enable building interop tests" ON)
if(ENABLE_INTEROP)
    add_subdirectory(test/interop)
endif()

# Performance / baseline tests (CPU hotspots, memory baselines, protocol micro-benchmarks)
option(ENABLE_PERF_TESTS "Enable building perf (test/perf) targets" ON)
if(ENABLE_PERF_TESTS)
    add_subdirectory(test/perf)
endif()

# ---------------------------------------------------------------------------
# Install rules
# ---------------------------------------------------------------------------
# Allow downstream consumers to either:
#   (a) `find_package(quicx 1.0.0 REQUIRED)` after `cmake --install`, or
#   (b) `add_subdirectory(quicX)` directly in their build tree.
#
# Layout after `cmake --install <build> --prefix <prefix>`:
#   <prefix>/include/quicx/{common,quic,http3,upgrade}/*.h    -- public headers
#   <prefix>/lib/libquicx.a                                   -- QUIC transport
#   <prefix>/lib/libhttp3.a                                   -- QUIC + HTTP/3
#   <prefix>/lib/cmake/quicx/quicxConfig.cmake                -- find_package entry
#   <prefix>/lib/cmake/quicx/quicxConfigVersion.cmake         -- version check
#   <prefix>/lib/cmake/quicx/quicxTargets.cmake               -- imported targets
#
# Note: BoringSSL (crypto/ssl) is statically linked into the archives.
# Downstream consumers do NOT need to install or link BoringSSL separately.
# ---------------------------------------------------------------------------
option(QUICX_INSTALL "Generate install rules for quicx" ON)

if(QUICX_INSTALL)
    include(GNUInstallDirs)
    include(CMakePackageConfigHelpers)

    set(QUICX_INSTALL_CMAKEDIR "${CMAKE_INSTALL_LIBDIR}/cmake/quicx"
        CACHE STRING "Install path for quicx CMake package files")

    # Install public headers (the entire <quicx/...> tree).
    install(DIRECTORY ${PROJECT_SOURCE_DIR}/include/quicx
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
        FILES_MATCHING PATTERN "*.h"
    )

    # Install the two static libraries and record them in the export set.
    install(TARGETS quicx http3
        EXPORT quicxTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    )

    # Export the targets file (consumed by quicxConfig.cmake).
    install(EXPORT quicxTargets
        FILE quicxTargets.cmake
        NAMESPACE quicx::
        DESTINATION ${QUICX_INSTALL_CMAKEDIR}
    )

    # Generate quicxConfigVersion.cmake from the project version.
    write_basic_package_version_file(
        "${CMAKE_CURRENT_BINARY_DIR}/quicxConfigVersion.cmake"
        VERSION ${PROJECT_VERSION}
        COMPATIBILITY SameMinorVersion
    )

    # Configure quicxConfig.cmake from the template.
    configure_package_config_file(
        "${PROJECT_SOURCE_DIR}/cmake/quicxConfig.cmake.in"
        "${CMAKE_CURRENT_BINARY_DIR}/quicxConfig.cmake"
        INSTALL_DESTINATION ${QUICX_INSTALL_CMAKEDIR}
    )

    install(FILES
        "${CMAKE_CURRENT_BINARY_DIR}/quicxConfig.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/quicxConfigVersion.cmake"
        DESTINATION ${QUICX_INSTALL_CMAKEDIR}
    )
endif()
