cmake_minimum_required(VERSION 3.10)

project(server_push)
add_executable(${PROJECT_NAME} server.cpp)
target_link_libraries(${PROJECT_NAME} PRIVATE http3)

project(client_push)
add_executable(${PROJECT_NAME} client.cpp)
target_link_libraries(${PROJECT_NAME} PRIVATE http3)
