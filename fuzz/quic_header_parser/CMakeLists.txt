# QUIC Long Header Fuzz Test

# Create long header fuzz test executable
add_executable(long_header_fuzz
    long_header_fuzz.cpp
)

# Include project headers (so we can include "common/..." etc.)
target_include_directories(long_header_fuzz PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

# Link against core library
target_link_libraries(long_header_fuzz PRIVATE quicx)

# Enable libFuzzer sanitizers only when requested
if(ENABLE_LIBFUZZER)
    target_compile_options(long_header_fuzz PRIVATE -fsanitize=fuzzer,address,undefined)
    target_link_options(long_header_fuzz PRIVATE -fsanitize=fuzzer,address,undefined)
else()
    # Provide a tiny main() for gdb mode
    target_sources(long_header_fuzz PRIVATE main_gdb.cpp)
endif()

# Set C++ standard
set_target_properties(long_header_fuzz PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)
