cmake_minimum_required(VERSION 3.16)

# Metrics Monitoring Example
add_executable(metrics_monitoring_server server.cpp)
target_link_libraries(metrics_monitoring_server http3)
set_target_properties(metrics_monitoring_server PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/bin
)
# PERF VALIDATION: example accesses internal SetDefaultInitialRtt() to mirror
# what e2e_perf_test.cpp / example/file_transfer do for loopback benchmarking.
target_include_directories(metrics_monitoring_server PRIVATE ${PROJECT_SOURCE_DIR}/src)

add_executable(metrics_monitoring_client client.cpp)
target_link_libraries(metrics_monitoring_client http3)
set_target_properties(metrics_monitoring_client PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/bin
)
target_include_directories(metrics_monitoring_client PRIVATE ${PROJECT_SOURCE_DIR}/src)
