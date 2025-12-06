cmake_minimum_required(VERSION 3.10)

add_executable(bidirectional_server server.cpp)
target_link_libraries(bidirectional_server
    http3
    ${OPENSSL_LIBRARIES}
)

add_executable(bidirectional_client client.cpp)
target_link_libraries(bidirectional_client
    http3
    ${OPENSSL_LIBRARIES}
)

install(TARGETS bidirectional_server bidirectional_client
    RUNTIME DESTINATION bin
)
