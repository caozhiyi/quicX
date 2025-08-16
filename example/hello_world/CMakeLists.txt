cmake_minimum_required(VERSION 3.10)

project(hello_world_server)
add_executable(${PROJECT_NAME} server.cpp)

target_link_libraries(${PROJECT_NAME} PRIVATE http3)

project(hello_world_client)
add_executable(${PROJECT_NAME} client.cpp)
target_link_libraries(${PROJECT_NAME} PRIVATE http3)
