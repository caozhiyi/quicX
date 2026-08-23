# MSVC / clang-cl reject GCC-style -Wno-* flags (D8021); route to /wd#### instead.
add_compile_options(
    "$<$<OR:$<CXX_COMPILER_ID:MSVC>,$<STREQUAL:$<CXX_COMPILER_FRONTEND_VARIANT>,MSVC>>:/wd4101;/wd4189;/wd4505>"
    "$<$<AND:$<NOT:$<CXX_COMPILER_ID:MSVC>>,$<NOT:$<STREQUAL:$<CXX_COMPILER_FRONTEND_VARIANT>,MSVC>>>:-Wno-unused-variable;-Wno-unused-but-set-variable;-Wno-unused-function>"
)

add_subdirectory(hello_world)
add_subdirectory(server_push)
add_subdirectory(restful_api)
add_subdirectory(concurrent_requests)
add_subdirectory(quicx_curl)
add_subdirectory(streaming_api)
add_subdirectory(file_transfer)
add_subdirectory(error_handling)
add_subdirectory(connection_lifecycle)
add_subdirectory(performance_benchmark)
add_subdirectory(bidirectional_comm)
add_subdirectory(load_testing)
add_subdirectory(metrics_monitoring)
add_subdirectory(qlog_integration)
add_subdirectory(static_server)
add_subdirectory(upgrade_h3)
