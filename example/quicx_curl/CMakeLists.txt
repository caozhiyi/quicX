cmake_minimum_required(VERSION 3.10)

# quicx-curl - HTTP/3 command-line tool
add_executable(quicx-curl
    main.cpp
    args_parser.cpp
    http_client.cpp
    output_formatter.cpp
)

target_link_libraries(quicx-curl
    http3
    quicx
)

install(
    TARGETS quicx-curl
    RUNTIME DESTINATION bin
)

