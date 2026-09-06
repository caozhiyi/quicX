# MSVC / clang-cl reject GCC-style -Wno-* flags (D8021); route to /wd#### instead.
# Uses the if() variable form: the $<CXX_COMPILER_FRONTEND_VARIANT> generator
# expression only exists in CMake >= 3.30 and hard-fails configure on older
# versions, while the CMAKE_CXX_COMPILER_FRONTEND_VARIANT variable has been
# set by CMake since 3.0.
if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC" OR CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
    add_compile_options(/wd4101 /wd4189 /wd4505)
else()
    add_compile_options(-Wno-unused-variable -Wno-unused-but-set-variable -Wno-unused-function)
endif()

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
