#include <gtest/gtest.h>

#include "quic/crypto/tls/tls_ctx.h"

namespace quicx {
namespace quic {

TEST(CryptoSslCtxTest, test1) {
    TLSCtx ctx;
    EXPECT_TRUE(ctx.Init(false));
}

}  // namespace quicx
}  // namespace quicx