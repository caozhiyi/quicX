cmake_minimum_required(VERSION 3.16)

# Use googletest from the project
set(GTEST_ROOT ${CMAKE_SOURCE_DIR}/third/googletest)
if(EXISTS ${GTEST_ROOT}/CMakeLists.txt)
  if(NOT TARGET gtest)
    add_subdirectory(${GTEST_ROOT} ${CMAKE_BINARY_DIR}/test/congestion_control/build_gtest EXCLUDE_FROM_ALL)
  endif()
else()
  message(FATAL_ERROR "third/googletest submodule missing. Please init submodules.")
endif()

# Congestion control test sources
set(CC_TEST_SOURCES
    network_simulator.cpp
    cc_test_framework.cpp
    cc_comprehensive_test.cpp
    cc_algorithm_validation_test.cpp
    cc_bbr_detailed_test.cpp
    cc_realistic_network_test.cpp
)

add_executable(cc_test ${CC_TEST_SOURCES})

target_include_directories(cc_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/test/congestion_control
)

target_link_libraries(cc_test PRIVATE
    http3
    GTest::gtest
    GTest::gtest_main
)

# Set output directory
set_target_properties(cc_test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/test
)

# Add to CTest
enable_testing()
add_test(NAME CongestionControlTest COMMAND cc_test)