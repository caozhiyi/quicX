#include <gtest/gtest.h>
#include "quic/crypto/tls/tls_ctx.h"

namespace quicx {
namespace quic {

TEST(crypto_ssl_ctx_utest, test1) {
    TLSCtx ctx;
    EXPECT_TRUE(ctx.Init());
}

}
}