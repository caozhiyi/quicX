cmake_minimum_required(VERSION 3.10)

project(hello_world_server)
add_executable(${PROJECT_NAME} server.cpp)

if(WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE http3 crypto ssl ws2_32)
else()
    target_link_libraries(${PROJECT_NAME} PRIVATE http3 crypto ssl pthread)
endif()

project(hello_world_client)
add_executable(${PROJECT_NAME} client.cpp)
if(WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE http3 crypto ssl ws2_32)
else()
    target_link_libraries(${PROJECT_NAME} PRIVATE http3 crypto ssl pthread)
endif()
