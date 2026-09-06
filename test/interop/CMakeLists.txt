# CMakeLists.txt for QUIC interop test programs

cmake_minimum_required(VERSION 3.10)
# MSVC / clang-cl reject GCC-style -Wno-* flags (D8021); route to /wd#### instead.
# Configure-time variables instead of genex for CMake >= 3.10 compat (docker builder).
if(MSVC OR CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
    add_compile_options(/wd4101 /wd4189 /wd4505)
else()
    add_compile_options(-Wno-unused-variable -Wno-unused-but-set-variable -Wno-unused-function)
endif()

# Interop Server (links http3 library for HTTP/3 support)
add_executable(interop_server
    interop_server.cpp
)

target_link_libraries(interop_server
    http3
)

target_include_directories(interop_server PRIVATE
    ${PROJECT_SOURCE_DIR}/src
)

# Export dynamic symbols so the built-in crash handler's backtrace_symbols_fd()
# can resolve function names instead of printing bare addresses.
set_target_properties(interop_server PROPERTIES ENABLE_EXPORTS ON)

# Interop Client (links http3 library for HTTP/3 support)
add_executable(interop_client
    interop_client.cpp
)

target_link_libraries(interop_client
    http3
)

target_include_directories(interop_client PRIVATE
    ${PROJECT_SOURCE_DIR}/src
)

# Install binaries
install(TARGETS interop_server interop_client
    RUNTIME DESTINATION bin
)
