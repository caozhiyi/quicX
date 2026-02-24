cmake_minimum_required(VERSION 3.16)

# QLog Integration Example
add_executable(qlog_server server.cpp)
target_link_libraries(qlog_server http3)
set_target_properties(qlog_server PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/bin
)

add_executable(qlog_client client.cpp)
target_link_libraries(qlog_client http3)
set_target_properties(qlog_client PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/bin
)
