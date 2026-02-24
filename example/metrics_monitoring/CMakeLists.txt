cmake_minimum_required(VERSION 3.16)

# Metrics Monitoring Example
add_executable(metrics_monitoring_server server.cpp)
target_link_libraries(metrics_monitoring_server http3)
set_target_properties(metrics_monitoring_server PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/bin
)

add_executable(metrics_monitoring_client client.cpp)
target_link_libraries(metrics_monitoring_client http3)
set_target_properties(metrics_monitoring_client PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/bin
)
