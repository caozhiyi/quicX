# Top of file
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

# Collect all test sources under utest
file(GLOB_RECURSE test_files
    ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
)

# Single test executable for all unit tests
add_executable(unit_test ${test_files})

target_include_directories(unit_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}
)

# Link both gtest and gtest_main libraries
target_link_libraries(unit_test PRIVATE http3 GTest::gtest GTest::gtest_main)

# Register the test with CTest
add_test(NAME unit_test COMMAND unit_test)