cmake_minimum_required(VERSION 3.10)

add_executable(load_tester load_tester.cpp)
target_link_libraries(load_tester
    http3
    ${OPENSSL_LIBRARIES}
)

install(TARGETS load_tester
    RUNTIME DESTINATION bin
)
