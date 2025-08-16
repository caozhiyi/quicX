cmake_minimum_required(VERSION 3.16)

add_executable(upgrade_h3_server
    server.cpp
)

target_include_directories(upgrade_h3_server
    PRIVATE
        ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(upgrade_h3_server
    PRIVATE
        http3
)

set_target_properties(upgrade_h3_server PROPERTIES
    OUTPUT_NAME upgrade_h3_server
)


