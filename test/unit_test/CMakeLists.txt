# Top of file
# MSVC / clang-cl reject GCC-style -Wno-* flags (D8021); route to /wd#### instead.
# Uses the if() variable form: the $<CXX_COMPILER_FRONTEND_VARIANT> generator
# expression only exists in CMake >= 3.30 and hard-fails configure on older
# versions, while the CMAKE_CXX_COMPILER_FRONTEND_VARIANT variable has been
# set by CMake since 3.0.
if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC" OR CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
    add_compile_options(/wd4101 /wd4189 /wd4505)
else()
    add_compile_options(-Wno-unused-variable -Wno-unused-but-set-variable -Wno-unused-function)
endif()
cmake_minimum_required(VERSION 3.16)
project(quicx_utest)

# Enable CTest for this directory
enable_testing()

# Use submodule googletest
set(GTEST_ROOT ${CMAKE_SOURCE_DIR}/third/googletest)
if(EXISTS ${GTEST_ROOT}/CMakeLists.txt)
  add_subdirectory(${GTEST_ROOT} build_gtest EXCLUDE_FROM_ALL)
else()
  message(FATAL_ERROR "third/googletest submodule missing. Please init submodules or set GTest_DIR.")
endif()

# Collect all test sources under unit_test
file(GLOB_RECURSE test_files
    ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
)

# Single test executable for all unit tests
add_executable(quicx_utest ${test_files})

target_include_directories(quicx_utest PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}
    ${boringssl_SOURCE_DIR}/include
)

# Link gtest, gmock, and gtest_main libraries
target_link_libraries(quicx_utest PRIVATE http3 GTest::gtest GTest::gmock GTest::gtest_main)

# Register the test with CTest
add_test(NAME quicx_utest COMMAND quicx_utest)