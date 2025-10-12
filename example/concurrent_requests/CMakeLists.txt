cmake_minimum_required(VERSION 3.10)

# concurrent requests server
add_executable(concurrent_server
    server.cpp
)

target_link_libraries(concurrent_server
    http3
    quicx
)

# concurrent requests client
add_executable(concurrent_client
    client.cpp
)

target_link_libraries(concurrent_client
    http3
    quicx
)

install(
    TARGETS concurrent_server concurrent_client
    RUNTIME DESTINATION bin
)

