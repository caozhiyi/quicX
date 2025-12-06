cmake_minimum_required(VERSION 3.10)

add_executable(performance_benchmark benchmark.cpp)
target_link_libraries(performance_benchmark
    http3
    ${OPENSSL_LIBRARIES}
)

install(TARGETS performance_benchmark
    RUNTIME DESTINATION bin
)
