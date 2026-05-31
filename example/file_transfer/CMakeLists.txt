cmake_minimum_required(VERSION 3.10)

# Add server executable
add_executable(file_transfer_server server.cpp)
target_link_libraries(file_transfer_server
    http3
    ${OPENSSL_LIBRARIES}
)
# PERF VALIDATION: example accesses internal SetDefaultInitialRtt() to mirror
# what e2e_perf_test.cpp does for loopback benchmarking.
target_include_directories(file_transfer_server PRIVATE ${PROJECT_SOURCE_DIR}/src)

# Add client executable
add_executable(file_transfer_client client.cpp)
target_link_libraries(file_transfer_client
    http3
    ${OPENSSL_LIBRARIES}
)
target_include_directories(file_transfer_client PRIVATE ${PROJECT_SOURCE_DIR}/src)

# Install targets
install(TARGETS file_transfer_server file_transfer_client
    RUNTIME DESTINATION bin
)
