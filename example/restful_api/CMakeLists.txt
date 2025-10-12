cmake_minimum_required(VERSION 3.10)

# restful api server
add_executable(restful_api_server
    server.cpp
)

target_link_libraries(restful_api_server
    http3
    quicx
)

# restful api client
add_executable(restful_api_client
    client.cpp
)

target_link_libraries(restful_api_client
    http3
    quicx
)

install(
    TARGETS restful_api_server restful_api_client
    RUNTIME DESTINATION bin
)

