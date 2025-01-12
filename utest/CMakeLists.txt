cmake_minimum_required(VERSION 3.10)

collect_sources(${PROJECT_SOURCE_DIR}/utest test_files)

project(quicx_utest)
add_executable(${PROJECT_NAME} ${test_files} test.cpp)

if(WIN32)
    target_link_libraries(${PROJECT_NAME}
        PRIVATE
        http3
        crypto
        ssl
        ws2_32
        gtest
    )
else()
    target_link_libraries(${PROJECT_NAME}
        PRIVATE
        http3
        crypto
        ssl
        pthread
        gtest
    )
endif()