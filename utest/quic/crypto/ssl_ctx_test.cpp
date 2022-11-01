#include <gtest/gtest.h>
#include "quic/crypto/ssl_ctx.h"

TEST(crypto_ssl_ctx_utest, test1) {
    std::string ciphers = "ECDHE-ECDSA-CHACHA20-POLY1305:ECDHE-ECDSA-AES128-GCM-SHA256:ECDHE-ECDSA-AES128-SHA256:ECDHE-RSA-CHACHA20-POLY1305:ECDHE-RSA-AES128-GCM-SHA256:ECDHE-RSA-AES128-SHA256";
    
    quicx::SSLCtx ctx;
    EXPECT_TRUE(ctx.Init());
}