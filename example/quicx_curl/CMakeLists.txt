# HTTP/3 command-line tool (curl-like + QUIC feature showcase)

add_executable(quicx_curl
    main.cpp
    cli/option_table.cpp
    app/orchestrator.cpp
    app/request_builder.cpp
    app/request_driver.cpp
    session/session.cpp
    session/transport_map.cpp
    features/redirect.cpp
    features/cookie_jar.cpp
    features/migration_ctl.cpp
    features/push_display.cpp
    io/sinks.cpp
    io/progress.cpp
    io/writeout.cpp
    obs/stats_report.cpp
)

target_include_directories(quicx_curl PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(quicx_curl
    http3
    quicx
    ${OPENSSL_LIBRARIES}
)
