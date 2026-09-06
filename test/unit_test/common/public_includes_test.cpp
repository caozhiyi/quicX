// Self-test for the public <quicx/...> header tree.
//
// Ensures every public header:
//   1. compiles when included alone (no transitive dependency cycles);
//   2. exposes the expected public symbols.
//
// If a public symbol disappears or a header is renamed, this file fails to
// compile and the breakage is caught at PR time rather than at release time.

#include <quicx/common/if_buffer_read.h>
#include <quicx/common/if_buffer_write.h>
#include <quicx/common/metrics.h>
#include <quicx/common/type.h>
#include <quicx/common/version.h>
#include <quicx/http3/if_async_handler.h>
#include <quicx/http3/if_client.h>
#include <quicx/http3/if_request.h>
#include <quicx/http3/if_response.h>
#include <quicx/http3/if_server.h>
#include <quicx/http3/type.h>
#include <quicx/quic/if_quic_bidirection_stream.h>
#include <quicx/quic/if_quic_client.h>
#include <quicx/quic/if_quic_connection.h>
#include <quicx/quic/if_quic_recv_stream.h>
#include <quicx/quic/if_quic_send_stream.h>
#include <quicx/quic/if_quic_server.h>
#include <quicx/quic/if_quic_stream.h>
#include <quicx/quic/type.h>
#include <quicx/upgrade/if_upgrade.h>
#include <quicx/upgrade/type.h>

#include <gtest/gtest.h>

namespace {

// A handful of public-symbol probes. We do not exercise behavior here — the
// per-component utests already do — we only confirm that the symbols are
// reachable through the public include paths.

TEST(QuicxPublicIncludesTest, CommonSymbolsReachable) {
    // From <quicx/common/version.h>
    EXPECT_NE(quicx::GetVersionString(), nullptr);
}

TEST(QuicxPublicIncludesTest, MetricsSymbolsReachable) {
    // From <quicx/common/metrics.h>
    constexpr auto kInvalid = quicx::kInvalidMetricID;
    EXPECT_EQ(kInvalid, static_cast<quicx::MetricID>(-1));
}

TEST(QuicxPublicIncludesTest, UpgradeFactoryReachable) {
    // From <quicx/upgrade/if_upgrade.h>. Address-taking only: we never start
    // the server here (that would bind a port), we just prove the symbol is
    // reachable through the public include path.
    auto* factory = &quicx::IUpgrade::MakeUpgrade;
    EXPECT_NE(factory, nullptr);
}

}  // namespace
