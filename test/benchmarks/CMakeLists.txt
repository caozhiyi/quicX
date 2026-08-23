cmake_minimum_required(VERSION 3.16)
# MSVC / clang-cl reject GCC-style -Wno-* flags (D8021); route to /wd#### instead.
add_compile_options(
    "$<$<OR:$<CXX_COMPILER_ID:MSVC>,$<STREQUAL:$<CXX_COMPILER_FRONTEND_VARIANT>,MSVC>>:/wd4101;/wd4189;/wd4505>"
    "$<$<AND:$<NOT:$<CXX_COMPILER_ID:MSVC>>,$<NOT:$<STREQUAL:$<CXX_COMPILER_FRONTEND_VARIANT>,MSVC>>>:-Wno-unused-variable;-Wno-unused-but-set-variable;-Wno-unused-function>"
)

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

function(add_quicx_benchmark target src)
    add_executable(${target} ${src})
    # http3 PUBLIC-links quicx, which itself transitively brings in
    # Threads / filesystem / BoringSSL crypto+ssl. Listing them again here
    # is redundant; just link http3.
    target_link_libraries(${target} PRIVATE benchmark::benchmark http3)
    target_include_directories(${target} PRIVATE ${PROJECT_SOURCE_DIR}/src)
    target_compile_definitions(${target} PRIVATE NDEBUG QUICX_ENABLE_BENCHMARKS)
    target_compile_options(${target} PRIVATE
        "$<$<AND:$<NOT:$<CXX_COMPILER_ID:MSVC>>,$<NOT:$<STREQUAL:$<CXX_COMPILER_FRONTEND_VARIANT>,MSVC>>>:-O3>"
        "$<$<OR:$<CXX_COMPILER_ID:MSVC>,$<STREQUAL:$<CXX_COMPILER_FRONTEND_VARIANT>,MSVC>>:/O2>"
    )
    set_target_properties(${target} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/benchmarks)
endfunction()

# Benchmarks
add_quicx_benchmark(qpack_bench qpack_bench.cpp)
add_quicx_benchmark(buffer_bench buffer_bench.cpp)
add_quicx_benchmark(qpack_decode_bench qpack_decode_bench.cpp)
add_quicx_benchmark(qpack_huffman_bench qpack_huffman_bench.cpp)
add_quicx_benchmark(varint_bench varint_bench.cpp)
add_quicx_benchmark(memorypool_bench memorypool_bench.cpp)
add_quicx_benchmark(timer_bench timer_bench.cpp)
add_quicx_benchmark(quic_frame_bench quic_frame_bench.cpp)
add_quicx_benchmark(quic_aead_bench quic_aead_bench.cpp)
add_quicx_benchmark(congestion_bench congestion_bench.cpp)
add_quicx_benchmark(pto_bench pto_bench.cpp)
add_quicx_benchmark(http3_e2e_bench http3_e2e_bench.cpp)
add_quicx_benchmark(qpack_instr_bench qpack_instr_bench.cpp)
add_quicx_benchmark(quic_ack_maxdata_bench quic_ack_maxdata_bench.cpp)
add_quicx_benchmark(qlog_overhead_bench qlog_overhead_bench.cpp)
add_quicx_benchmark(metrics_bench metrics_bench.cpp)
add_quicx_benchmark(allocator_smartptr_bench allocator_smartptr_bench.cpp)

# cmake -S .. -B build -DCMAKE_BUILD_TYPE=Release -DENABLE_BENCHMARKS=ON && cmake --build build -j