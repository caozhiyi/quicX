cmake_minimum_required(VERSION 3.10)

# Add server executable
add_executable(error_handling_server server.cpp)
target_link_libraries(error_handling_server
    http3
    ${OPENSSL_LIBRARIES}
)

# Add client executable
add_executable(error_handling_client client.cpp)
target_link_libraries(error_handling_client
    http3
    ${OPENSSL_LIBRARIES}
)

# Install targets
install(TARGETS error_handling_server error_handling_client
    RUNTIME DESTINATION bin
)
