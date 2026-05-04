cmake_minimum_required(VERSION 3.10)

# Integration tests
add_executable(http3_methods_test http3_methods_test.cpp)
target_link_libraries(http3_methods_test
    http3
    gtest
    gtest_main
    ${OPENSSL_LIBRARIES}
)

add_executable(connection_management_test connection_management_test.cpp)
target_link_libraries(connection_management_test
    http3
    gtest
    gtest_main
    ${OPENSSL_LIBRARIES}
)

add_executable(error_handling_test error_handling_test.cpp)
target_link_libraries(error_handling_test
    http3
    gtest
    gtest_main
    ${OPENSSL_LIBRARIES}
)

add_executable(stress_test stress_test.cpp)
target_link_libraries(stress_test
    http3
    gtest
    gtest_main
    ${OPENSSL_LIBRARIES}
)

add_executable(advanced_features_test advanced_features_test.cpp)
target_link_libraries(advanced_features_test
    http3
    gtest
    gtest_main
    ${OPENSSL_LIBRARIES}
)

add_executable(streaming_and_push_test streaming_and_push_test.cpp)
target_link_libraries(streaming_and_push_test
    http3
    gtest
    gtest_main
    ${OPENSSL_LIBRARIES}
)

# Add tests to CTest
add_test(NAME HTTP3Methods COMMAND http3_methods_test)
add_test(NAME ConnectionManagement COMMAND connection_management_test)
add_test(NAME ErrorHandling COMMAND error_handling_test)
add_test(NAME StressTest COMMAND stress_test)
add_test(NAME AdvancedFeatures COMMAND advanced_features_test)
add_test(NAME StreamingAndPush COMMAND streaming_and_push_test)

# Install targets
install(TARGETS http3_methods_test connection_management_test error_handling_test stress_test
    advanced_features_test streaming_and_push_test
    RUNTIME DESTINATION bin/test
)
