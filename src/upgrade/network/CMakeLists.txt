# Event driver sources - platform specific
if(WIN32)
    set(EVENT_DRIVER_SOURCES
        windows/select_event_driver.cpp
    )
elseif(APPLE)
    set(EVENT_DRIVER_SOURCES
        macos/kqueue_event_driver.cpp
    )
else()
    set(EVENT_DRIVER_SOURCES
        linux/epoll_event_driver.cpp
    )
endif()

# Common sources
set(COMMON_SOURCES
    if_event_driver.cpp
    tcp_action.cpp
    tcp_socket.cpp
)

# Create library
add_library(upgrade_network STATIC
    ${COMMON_SOURCES}
    ${EVENT_DRIVER_SOURCES}
)

# Link libraries
if(WIN32)
    target_link_libraries(upgrade_network PRIVATE ws2_32 mswsock)
endif() 