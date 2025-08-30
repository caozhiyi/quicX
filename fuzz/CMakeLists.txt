cmake_minimum_required(VERSION 3.16)

# Set project name
project(QUICFuzzTests)

# Options to control fuzz build
option(ENABLE_LIBFUZZER "Build fuzzers with libFuzzer sanitizers" ON)
option(FUZZ_USE_CLANG "Use clang/clang++ for fuzzers" ON)

if(FUZZ_USE_CLANG)
    if(NOT CMAKE_CXX_COMPILER MATCHES ".*clang\+\+")
        message(STATUS "Setting C and CXX to clang/clang++ for fuzzers")
        set(CMAKE_C_COMPILER "clang" CACHE STRING "" FORCE)
        set(CMAKE_CXX_COMPILER "clang++" CACHE STRING "" FORCE)
    endif()
endif()

# Fuzz testing configuration
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Add QUIC long header fuzz test
add_subdirectory(quic_header_parser)
