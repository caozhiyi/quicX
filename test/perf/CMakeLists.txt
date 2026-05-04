cmake_minimum_required(VERSION 3.16)

# ===========================================================================
# Performance test targets for quicX
# Includes: CPU hotspot tests, memory baseline tests, memory pool analysis
# ===========================================================================

include(FetchContent)

# Fetch Google Benchmark (shared with test/benchmarks/)
FetchContent_Declare(
  googlebenchmark
  GIT_REPOSITORY https://github.com/google/benchmark.git
  GIT_TAG v1.8.3
)
set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
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
    target_compile_options(${target} PRIVATE -O2 -g -fno-omit-frame-pointer)
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
