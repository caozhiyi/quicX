cmake_minimum_required(VERSION 3.12)

# Server
add_executable(streaming_server server.cpp)
# http3 PUBLIC-links quicx + BoringSSL (crypto/ssl); no need to repeat them.
target_link_libraries(streaming_server PRIVATE http3)
set_target_properties(streaming_server PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/bin)

# Client
add_executable(streaming_client client.cpp)
target_link_libraries(streaming_client PRIVATE http3)
set_target_properties(streaming_client PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/bin)

