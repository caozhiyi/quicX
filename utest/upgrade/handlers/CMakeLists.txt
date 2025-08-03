# Handlers component unit tests
set(HANDLERS_TEST_SOURCES
    main_test.cpp
    connection_context_test.cpp
    smart_handler_factory_test.cpp
    base_smart_handler_test.cpp
    integration_test.cpp
)

# Create test executable
add_executable(upgrade_handlers_test ${HANDLERS_TEST_SOURCES})

# Link against required libraries
target_link_libraries(upgrade_handlers_test
    gtest
    gtest_main
    pthread
    ssl
    crypto
)

# Include directories
target_include_directories(upgrade_handlers_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/third
)

# Add test to CTest
add_test(NAME UpgradeHandlersTest COMMAND upgrade_handlers_test)

# Set test properties
set_tests_properties(UpgradeHandlersTest PROPERTIES
    TIMEOUT 300
    ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR}/lib"
) 