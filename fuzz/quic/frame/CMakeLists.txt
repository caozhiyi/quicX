cmake_minimum_required(VERSION 3.16)

# Build all fuzzers in this directory: any file matching *_fuzz.cpp
file(GLOB FUZZ_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/*_fuzz.cpp
)

foreach(src ${FUZZ_SOURCES})
    get_filename_component(name ${src} NAME_WE)
    add_executable(${name} ${src})

    target_include_directories(${name} PRIVATE
        ${CMAKE_SOURCE_DIR}/src
    )

    target_link_libraries(${name} PRIVATE quicx)

    # Always enable libFuzzer + ASan/UBSan for fuzz targets
    target_compile_options(${name} PRIVATE -fsanitize=fuzzer,address,undefined)
    target_link_options(${name} PRIVATE -fsanitize=fuzzer,address,undefined)

    set_target_properties(${name} PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED ON
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/fuzz
    )
endforeach()



