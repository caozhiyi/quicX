cmake_minimum_required(VERSION 3.10)

collect_sources(${PROJECT_SOURCE_DIR}/utest test_files)

project(quicx_utest)

# Download and build GTest
include(FetchContent)
FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG release-1.11.0
)
FetchContent_MakeAvailable(googletest)

include(GoogleTest)

enable_testing()
add_executable(${PROJECT_NAME} ${test_files} test.cpp)

if(WIN32)
    target_link_libraries(${PROJECT_NAME}
        PRIVATE
        http3
        crypto
        ssl
        ws2_32
        ${GTEST_LIBRARIES}
    )
else()
    target_link_libraries(${PROJECT_NAME}
        PRIVATE
        http3
        crypto
        ssl
        pthread
        ${GTEST_LIBRARIES}
    )
endif()