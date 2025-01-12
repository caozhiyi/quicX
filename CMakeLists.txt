cmake_minimum_required(VERSION 3.10)
project(QuicX)

# Set C++ standard
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Output path
set(EXECUTABLE_OUTPUT_PATH ${PROJECT_BINARY_DIR}/bin)
set(LIBRARY_OUTPUT_PATH ${PROJECT_BINARY_DIR}/lib)

# Download and build BoringSSL
include(FetchContent)
FetchContent_Declare(
  boringssl
  GIT_REPOSITORY https://github.com/google/boringssl.git
  GIT_TAG master
)
FetchContent_MakeAvailable(boringssl)

# Download and build GTest
FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG release-1.11.0
)
FetchContent_MakeAvailable(googletest)

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

# Include directories
include_directories(
    ${PROJECT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/third
    ${PROJECT_SOURCE_DIR}/third/boringssl/include
)

# Add subdirectories for source files
add_subdirectory(src)

# Define static libraries
add_library(quicx STATIC ${common_source} ${quic_source})
add_library(http3 STATIC ${http3_source} ${common_source} ${quic_source})

#enable_testing()
#add_subdirectory(utest)
add_subdirectory(example)
