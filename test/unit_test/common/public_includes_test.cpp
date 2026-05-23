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
#include <quicx/common/if_event_loop.h>
#include <quicx/common/metrics.h>
#include <quicx/common/metrics_std.h>
#include <quicx/common/type.h>
#include <quicx/common/version.h>

#include <quicx/quic/if_quic_bidirection_stream.h>
#include <quicx/quic/if_quic_client.h>
#include <quicx/quic/if_quic_connection.h>
#include <quicx/quic/if_quic_recv_stream.h>
#include <quicx/quic/if_quic_send_stream.h>
#include <quicx/quic/if_quic_server.h>
#include <quicx/quic/if_quic_stream.h>
#include <quicx/quic/type.h>

#include <quicx/http3/if_async_handler.h>
#include <quicx/http3/if_client.h>
#include <quicx/http3/if_request.h>
#include <quicx/http3/if_response.h>
#include <quicx/http3/if_server.h>
#include <quicx/http3/type.h>

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
    // From <quicx/common/metrics.h> and <quicx/common/metrics_std.h>
    constexpr auto kInvalid = quicx::common::kInvalidMetricID;
    EXPECT_EQ(kInvalid, static_cast<quicx::common::MetricID>(-1));
    // MetricsStd IDs are static members; we only check they are addressable.
    (void)&quicx::common::MetricsStd::UdpPacketsRx;
}

TEST(QuicxPublicIncludesTest, EventLoopFactoryReachable) {
    // From <quicx/common/if_event_loop.h>
    auto loop = quicx::common::MakeEventLoop();
    EXPECT_NE(loop, nullptr);
}

}  // namespace
