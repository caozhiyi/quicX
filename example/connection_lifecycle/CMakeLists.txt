cmake_minimum_required(VERSION 3.10)

# Add demo executable
add_executable(connection_lifecycle_demo demo.cpp)
target_link_libraries(connection_lifecycle_demo
    http3
    ${OPENSSL_LIBRARIES}
)

# Install target
install(TARGETS connection_lifecycle_demo
    RUNTIME DESTINATION bin
)
