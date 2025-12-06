cmake_minimum_required(VERSION 3.10)

# Add server executable
add_executable(file_transfer_server server.cpp)
target_link_libraries(file_transfer_server
    http3
    ${OPENSSL_LIBRARIES}
)

# Add client executable
add_executable(file_transfer_client client.cpp)
target_link_libraries(file_transfer_client
    http3
    ${OPENSSL_LIBRARIES}
)

# Install targets
install(TARGETS file_transfer_server file_transfer_client
    RUNTIME DESTINATION bin
)
