cmake_minimum_required(VERSION 3.16)

# Set project name
project(QUICFuzzTests)

# Options to control fuzz build
option(ENABLE_LIBFUZZER "Build fuzzers with libFuzzer sanitizers" ON)
option(FUZZ_USE_CLANG "Use clang/clang++ for fuzzers" ON)

if(FUZZ_USE_CLANG)
    if(NOT CMAKE_CXX_COMPILER MATCHES ".*clang\\+\\+")
        message(STATUS "Setting C and CXX to clang/clang++ for fuzzers")
        set(CMAKE_C_COMPILER "clang" CACHE STRING "" FORCE)
        set(CMAKE_CXX_COMPILER "clang++" CACHE STRING "" FORCE)
    endif()
endif()

# Fuzz testing configuration
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Add categorized fuzz suites
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/quic)
    add_subdirectory(quic)
endif()

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/http3)
    add_subdirectory(http3)
endif()

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/common)
    add_subdirectory(common)
endif()

# Backward compatibility: support legacy layout if present
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/quic_header_parser AND NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/quic/quic_header_parser)
    add_subdirectory(quic_header_parser)
endif()
