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
  GIT_TAG fips-20220613
)
FetchContent_MakeAvailable(boringssl)

# check if boringssl is populated
FetchContent_GetProperties(boringssl)
if(NOT boringssl_POPULATED)
    message(FATAL_ERROR "BoringSSL was not populated correctly")
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

# Include directories
include_directories(
    ${PROJECT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/src
    ${boringssl_SOURCE_DIR}/include
)

collect_sources(${PROJECT_SOURCE_DIR}/src/common common_files)
collect_sources(${PROJECT_SOURCE_DIR}/src/quic quic_files)
collect_sources(${PROJECT_SOURCE_DIR}/src/http3 http3_files)

SET(common_source ${common_files})
SET(quic_source ${quic_files})
SET(http3_source ${http3_files})

# Define static libraries
add_library(quicx STATIC ${common_source} ${quic_source})
add_library(http3 STATIC ${http3_source} ${common_source} ${quic_source})

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
add_subdirectory(example)

# Define an option to enable or disable testing
option(ENABLE_TESTING "Enable building of unit tests" ON)

# Enable testing if the option is set
if(ENABLE_TESTING)
    add_subdirectory(utest)
endif()