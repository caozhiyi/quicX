cmake_minimum_required(VERSION 3.10)

project(static_server)
add_executable(${PROJECT_NAME} server.cpp)

# Allow this example to include internal quicX headers (e.g.
# `common/log/stdout_logger.h`). The headers live under <repo>/src and are
# normally not part of the public API, but we use them here so the example
# can install a logger and surface upgrade-module diagnostics on stdout.
#
# Note: we use ${CMAKE_SOURCE_DIR} (the top-level repo) on purpose -- this
# subdirectory has its own `project(static_server)` which redefines
# PROJECT_SOURCE_DIR to point at this folder, not the repo root.
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

# `http3` is a single static lib that already bundles the upgrade module
# sources (see top-level CMakeLists.txt). Linking `http3` is enough.
target_link_libraries(${PROJECT_NAME} PRIVATE http3)
