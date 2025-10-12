cmake_minimum_required(VERSION 3.10)

# file transfer server
add_executable(file_transfer_server
    server.cpp
)

target_link_libraries(file_transfer_server
    http3
    quicx
)

# file transfer client
add_executable(file_transfer_client
    client.cpp
)

target_link_libraries(file_transfer_client
    http3
    quicx
)

install(
    TARGETS file_transfer_server file_transfer_client
    RUNTIME DESTINATION bin
)

