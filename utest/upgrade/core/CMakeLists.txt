# Upgrade Core Unit Tests CMakeLists.txt

# Find GTest
find_package(GTest REQUIRED)

# Create test executable
add_executable(upgrade_core_tests
    main_test.cpp
)

# Set C++ standard
set_target_properties(upgrade_core_tests PROPERTIES
    CXX_STANDARD 11
    CXX_STANDARD_REQUIRED ON
)

# Include directories
target_include_directories(upgrade_core_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/utest
)

# Link libraries
target_link_libraries(upgrade_core_tests
    GTest::gtest
    GTest::gtest_main
    gmock
)

# Add test
add_test(NAME UpgradeCoreTests COMMAND upgrade_core_tests)

# Set test properties
set_tests_properties(UpgradeCoreTests PROPERTIES
    TIMEOUT 300
    ENVIRONMENT "GTEST_COLOR=1"
) 