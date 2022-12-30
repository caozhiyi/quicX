#include <gtest/gtest.h>
#include "quic/crypto/tls/tls_ctx.h"

TEST(crypto_ssl_ctx_utest, test1) {
    quicx::TLSCtx ctx;
    EXPECT_TRUE(ctx.Init());
}