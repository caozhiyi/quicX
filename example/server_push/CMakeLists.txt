cmake_minimum_required(VERSION 3.10)

project(server_push)
add_executable(${PROJECT_NAME} server.cpp)

if(WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE http3 crypto ssl ws2_32)
else()
    target_link_libraries(${PROJECT_NAME} PRIVATE http3 crypto ssl pthread)
endif()

project(client_push)
add_executable(${PROJECT_NAME} client.cpp)
if(WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE http3 crypto ssl ws2_32)
else()
    target_link_libraries(${PROJECT_NAME} PRIVATE http3 crypto ssl pthread)
endif()
