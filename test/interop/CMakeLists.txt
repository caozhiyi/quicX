# CMakeLists.txt for QUIC interop test programs

cmake_minimum_required(VERSION 3.10)

# Interop Server
add_executable(interop_server
    interop_server.cpp
)

target_link_libraries(interop_server
    http3
)

target_include_directories(interop_server PRIVATE
    ${PROJECT_SOURCE_DIR}/src
)

# Interop Client
add_executable(interop_client
    interop_client.cpp
)

target_link_libraries(interop_client
    http3
)

target_include_directories(interop_client PRIVATE
    ${PROJECT_SOURCE_DIR}/src
)

# Install binaries
install(TARGETS interop_server interop_client
    RUNTIME DESTINATION bin
)
