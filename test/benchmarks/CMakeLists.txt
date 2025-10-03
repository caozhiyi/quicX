cmake_minimum_required(VERSION 3.16)

include(FetchContent)

# Fetch Google Benchmark
FetchContent_Declare(
  googlebenchmark
  GIT_REPOSITORY https://github.com/google/benchmark.git
  GIT_TAG v1.8.3
)
set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googlebenchmark)

function(add_quicx_benchmark target src)
    add_executable(${target} ${src})
    target_link_libraries(${target} PRIVATE benchmark::benchmark http3 quicx ssl crypto Threads::Threads)
    target_include_directories(${target} PRIVATE ${PROJECT_SOURCE_DIR}/src)
    target_compile_definitions(${target} PRIVATE NDEBUG QUICX_ENABLE_BENCHMARKS)
    target_compile_options(${target} PRIVATE -O3)
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
add_quicx_benchmark(buffer_chains_bench buffer_chains_bench.cpp)
add_quicx_benchmark(quic_frame_bench quic_frame_bench.cpp)
add_quicx_benchmark(quic_aead_bench quic_aead_bench.cpp)
add_quicx_benchmark(congestion_bench congestion_bench.cpp)
add_quicx_benchmark(pto_bench pto_bench.cpp)
add_quicx_benchmark(http3_e2e_bench http3_e2e_bench.cpp)
add_quicx_benchmark(qpack_instr_bench qpack_instr_bench.cpp)
add_quicx_benchmark(quic_ack_maxdata_bench quic_ack_maxdata_bench.cpp)

# cmake -S .. -B build -DCMAKE_BUILD_TYPE=Release -DENABLE_BENCHMARKS=ON && cmake --build build -j