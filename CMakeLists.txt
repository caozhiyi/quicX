cmake_minimum_required(VERSION 3.16)
project(QuicX)

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
        add_compile_options(/wd4996 /wd4267 /wd4244)
        add_definitions(-D_CRT_SECURE_NO_WARNINGS)
        add_definitions(-DWIN32_LEAN_AND_MEAN)
        add_definitions(-DNOMINMAX)
    endif()
endif()

function(collect_sources dir output_var)
    file(GLOB_RECURSE new_sources
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

# Only two static libraries per request
add_library(quicx STATIC ${common_source} ${quic_source})
add_library(http3 STATIC ${common_source} ${quic_source} ${upgrade_source} ${http3_source})

set_target_properties(quicx PROPERTIES POSITION_INDEPENDENT_CODE ON)
set_target_properties(http3 PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_include_directories(quicx
    PUBLIC
        ${PROJECT_SOURCE_DIR}/src
        ${boringssl_SOURCE_DIR}/include
)
target_include_directories(http3
    PUBLIC
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
        crypto
        ssl
        Threads::Threads
        ${FILESYSTEM_LIBRARY}
)
target_link_libraries(http3
    PUBLIC
        crypto
        ssl
        Threads::Threads
        ${FILESYSTEM_LIBRARY}
)

# Install headers
set(CUSTOM_INCLUDE_DIR ${CMAKE_BINARY_DIR}/include)
file(MAKE_DIRECTORY ${CUSTOM_INCLUDE_DIR})
file(GLOB_RECURSE PUBLIC_HEADERS 
    "${PROJECT_SOURCE_DIR}/src/http3/include/*.h"
    "${PROJECT_SOURCE_DIR}/src/quic/include/*.h"
)
foreach(HEADER ${PUBLIC_HEADERS})
    file(RELATIVE_PATH REL_PATH "${PROJECT_SOURCE_DIR}/src" ${HEADER})
    string(REPLACE "include/" "" REL_PATH ${REL_PATH})
    get_filename_component(DEST_DIR ${CUSTOM_INCLUDE_DIR}/${REL_PATH} DIRECTORY)
    file(MAKE_DIRECTORY ${DEST_DIR})
    file(COPY ${HEADER} DESTINATION ${DEST_DIR})
endforeach()

# Build examples
option(BUILD_EXAMPLES "Build examples" ON)
if(BUILD_EXAMPLES)
    add_subdirectory(example)
endif()

# Define an option to enable or disable testing
option(ENABLE_TESTING "Enable building of unit tests" ON)

# Enable testing if the option is set
if(ENABLE_TESTING)
    add_subdirectory(utest)
endif()

# Benchmarks
option(ENABLE_BENCHMARKS "Enable building benchmarks" ON)
if(ENABLE_BENCHMARKS)
    add_subdirectory(benchmarks)
endif()

# Fuzzing targets (libFuzzer)
option(ENABLE_FUZZING "Enable building fuzz targets" OFF)
if(ENABLE_FUZZING)
    add_subdirectory(fuzz)
endif()