cmake_minimum_required(VERSION 3.16)
project(quicx_install_test CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# This mini project simulates a downstream consumer of an installed quicx
# package. It is invoked by the CI / dev workflow as:
#
#   cmake --install <quicx-build>          --prefix <quicx-prefix>
#   cmake -S test/install-test -B <build>  -DCMAKE_PREFIX_PATH=<quicx-prefix> \
#         -DQUICX_TEST_BORINGSSL_LIB_DIR=<quicx-build>/lib
#
# The QUICX_TEST_BORINGSSL_LIB_DIR points at the BoringSSL static archives
# that quicx was built against; this models the "vendor BoringSSL" deployment
# pattern recommended in cmake/quicxConfig.cmake.in (Option A).
#
# If you do not pass QUICX_TEST_BORINGSSL_LIB_DIR, the smoke binary will try
# to link against whatever crypto/ssl find_package(OpenSSL) located, which
# will fail at link time if the system OpenSSL is missing BoringSSL-specific
# symbols (EVP_AEAD_*, etc.). This is by design: it documents the ABI
# constraint rather than hiding it.
#
# Provide BoringSSL's crypto/ssl as the OpenSSL imported targets quicx asked
# for. We must do this BEFORE find_package(quicx), so that quicxConfig.cmake
# sees the targets already exist and skips its own find_dependency(OpenSSL).
if(QUICX_TEST_BORINGSSL_LIB_DIR)
    if(NOT TARGET OpenSSL::Crypto)
        add_library(OpenSSL::Crypto STATIC IMPORTED)
        set_target_properties(OpenSSL::Crypto PROPERTIES
            IMPORTED_LOCATION "${QUICX_TEST_BORINGSSL_LIB_DIR}/libcrypto.a"
        )
    endif()
    if(NOT TARGET OpenSSL::SSL)
        add_library(OpenSSL::SSL STATIC IMPORTED)
        set_target_properties(OpenSSL::SSL PROPERTIES
            IMPORTED_LOCATION "${QUICX_TEST_BORINGSSL_LIB_DIR}/libssl.a"
            INTERFACE_LINK_LIBRARIES OpenSSL::Crypto
        )
    endif()
endif()

# Locate the installed quicx package. The caller is expected to pass:
#   -DCMAKE_PREFIX_PATH=<install-prefix>
find_package(quicx 0.1.0 REQUIRED)

# Sanity check: the imported targets must exist with the quicx:: namespace.
if(NOT TARGET quicx::quicx)
    message(FATAL_ERROR "find_package(quicx) did not provide target quicx::quicx")
endif()
if(NOT TARGET quicx::http3)
    message(FATAL_ERROR "find_package(quicx) did not provide target quicx::http3")
endif()

# Smoke binary that pulls in a representative slice of the public API.
# It only needs to compile + link successfully -- it is never run.
add_executable(smoke smoke.cpp)
target_link_libraries(smoke PRIVATE quicx::http3)
