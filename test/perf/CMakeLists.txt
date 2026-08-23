cmake_minimum_required(VERSION 3.16)
# MSVC / clang-cl reject GCC-style -Wno-* flags (D8021); route to /wd#### instead.
add_compile_options(
    "$<$<OR:$<CXX_COMPILER_ID:MSVC>,$<STREQUAL:$<CXX_COMPILER_FRONTEND_VARIANT>,MSVC>>:/wd4101;/wd4189;/wd4505>"
    "$<$<AND:$<NOT:$<CXX_COMPILER_ID:MSVC>>,$<NOT:$<STREQUAL:$<CXX_COMPILER_FRONTEND_VARIANT>,MSVC>>>:-Wno-unused-variable;-Wno-unused-but-set-variable;-Wno-unused-function>"
)

# ===========================================================================
# Performance test targets for quicX
# Includes: CPU hotspot tests, memory baseline tests, memory pool analysis
# ===========================================================================

include(FetchContent)

# Fetch Google Benchmark via release tarball (no git required — avoids
# 'Failed to get the hash for HEAD' failures on Windows CI).
FetchContent_Declare(
  googlebenchmark
  URL https://github.com/google/benchmark/archive/refs/tags/v1.8.3.tar.gz
)
set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_GTEST_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googlebenchmark)

# Common compile settings for perf tests
function(add_perf_test target src)
    add_executable(${target} ${src})
    target_link_libraries(${target}
        PRIVATE
            benchmark::benchmark
            http3
            quicx
            ssl
            crypto
            Threads::Threads
    )
    target_include_directories(${target} PRIVATE ${PROJECT_SOURCE_DIR}/src)
    target_compile_definitions(${target} PRIVATE QUICX_ENABLE_BENCHMARKS)
    # Use -O2 for perf tests (enough optimization for realistic profiling)
    if(MSVC)
        target_compile_options(${target} PRIVATE /O2 /Zi /Oy-)
    else()
        target_compile_options(${target} PRIVATE -O2 -g -fno-omit-frame-pointer)
    endif()
    set_target_properties(${target}
        PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/perf
    )
endfunction()

# Performance test targets (component-level)
add_perf_test(cpu_hotspot_test cpu_hotspot_test.cpp)
add_perf_test(memory_baseline_test memory_baseline_test.cpp)
add_perf_test(memory_pool_efficiency_test memory_pool_efficiency_test.cpp)

# Protocol hot-path micro-benchmarks (P0)
add_perf_test(crypto_perf_test crypto_perf_test.cpp)
add_perf_test(packet_perf_test packet_perf_test.cpp)
add_perf_test(congestion_control_perf_test congestion_control_perf_test.cpp)

# Frame / QPACK / recovery deep benchmarks (P1)
add_perf_test(frame_perf_test frame_perf_test.cpp)
add_perf_test(qpack_perf_test qpack_perf_test.cpp)
add_perf_test(loss_recovery_perf_test loss_recovery_perf_test.cpp)

# End-to-end full-chain performance tests
add_perf_test(e2e_perf_test e2e_perf_test.cpp)

# -----------------------------------------------------------------------------
# Ad-hoc profilers (not google-benchmark based). Built with -rdynamic so that
# glibc backtrace() can symbolize addresses inside the executable, which
# test/perf/tools/resolve_stacks.py then feeds through addr2line + c++filt
# to produce flamegraph-ready collapsed output.
# -----------------------------------------------------------------------------
function(add_profiler target src)
    if(WIN32)
        return()
    endif()
    add_executable(${target} ${src})
    target_link_libraries(${target}
        PRIVATE
            http3 quicx ssl crypto Threads::Threads
    )
    target_include_directories(${target} PRIVATE
        ${PROJECT_SOURCE_DIR}/src
        ${PROJECT_SOURCE_DIR}
    )
    target_compile_options(${target} PRIVATE
        -O2 -g -fno-omit-frame-pointer
    )
    # -rdynamic exports symbols into the dynamic table so backtrace_symbols()
    # can resolve in-executable frames.  We still use addr2line offline for
    # better accuracy (inlined frames, file:line).
    target_link_options(${target} PRIVATE -rdynamic)
    set_target_properties(${target} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/perf
    )
endfunction()

add_profiler(profile_decode_packets   tools/profile_decode_packets.cpp)
add_profiler(profile_qpack_encode     tools/profile_qpack_encode.cpp)
add_profiler(profile_blocked_registry tools/profile_blocked_registry.cpp)
add_profiler(profile_rss_lifecycle    tools/profile_rss_lifecycle.cpp)
