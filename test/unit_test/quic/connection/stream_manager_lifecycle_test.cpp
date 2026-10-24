#include <gtest/gtest.h>

#include <memory>
#include <string>

#include "common/network/if_event_loop.h"

#include "quic/connection/connection_stream_manager.h"
#include "quic/connection/controller/send_flow_controller.h"
#include "quic/connection/controller/send_manager.h"
#include "quic/connection/if_connection_event_sink.h"
#include "quic/connection/transport_param.h"
#include "quic/stream/if_stream.h"
#include "quic/stream/send_stream.h"

namespace quicx {
namespace quic {
namespace {

// Records every event the StreamManager can raise, so tests can assert both
// delivery (connection alive) and suppression (connection destroyed).
class CountingSink: public IConnectionEventSink {
public:
    void OnStreamDataReady(std::shared_ptr<IStream>) override { stream_data_ready++; }
    void OnFrameReady(std::shared_ptr<IFrame>) override { frame_ready++; }
    void OnConnectionActive() override { connection_active++; }
    void OnStreamClosed(uint64_t) override { stream_closed++; }
    void OnConnectionClose(uint64_t, uint16_t, const std::string&) override { connection_closed++; }

    int stream_data_ready = 0;
    int frame_ready = 0;
    int connection_active = 0;
    int stream_closed = 0;
    int connection_closed = 0;
};

struct Fixture {
    CountingSink sink;
    TransportParam tp;
    SendManager send_manager{nullptr};
    SendFlowController flow_controller{StreamIDGenerator::StreamStarter::kClient};
    std::shared_ptr<common::IEventLoop> loop;

    Fixture() {
        loop = common::MakeEventLoop();
        loop->Init();
    }
};

// Streams escape the connection as shared_ptr (the HTTP/3 layer holds them
// beyond the QUIC connection's lifetime). Once the connection — and with it
// the StreamManager — is destroyed, a late callback from such an escaped
// stream must be silently dropped, not dereference the dead manager.
TEST(StreamManagerLifecycleTest, LateCallbackAfterConnectionDeathIsDropped) {
    Fixture fx;

    std::shared_ptr<void> owner = std::make_shared<int>(0);  // connection stand-in
    std::shared_ptr<SendStream> escaped;
    {
        StreamManager mgr(fx.sink, fx.loop, fx.tp, fx.send_manager, nullptr, &fx.flow_controller);
        mgr.Bind(owner);
        escaped = std::dynamic_pointer_cast<SendStream>(mgr.MakeStream(1024, 4, StreamDirection::kSend));
        ASSERT_NE(escaped, nullptr);

        // Sanity while the connection is alive: Send() triggers the injected
        // active-send callback and the sink sees it.
        uint8_t data[4] = {1, 2, 3, 4};
        EXPECT_GT(escaped->Send(data, sizeof(data)), 0);
        EXPECT_EQ(fx.sink.stream_data_ready, 1);
    }
    // Scope end: StreamManager destroyed with its owning connection...
    owner.reset();  // ...and the life token expired.

    // Without the guard this dereferences the destroyed StreamManager's
    // event_sink_ (use-after-free; crashes under ASan). With the guard the
    // callback is dropped.
    uint8_t data[4] = {5, 6, 7, 8};
    EXPECT_GT(escaped->Send(data, sizeof(data)), 0);
    EXPECT_EQ(fx.sink.stream_data_ready, 1);  // unchanged: late callback dropped
    SUCCEED();
}

// Token alone is not enough to suppress callbacks while the connection (and
// StreamManager) is alive: normal delivery must keep working.
TEST(StreamManagerLifecycleTest, CallbackDeliveredWhileConnectionAlive) {
    Fixture fx;

    auto owner = std::make_shared<int>(0);
    StreamManager mgr(fx.sink, fx.loop, fx.tp, fx.send_manager, nullptr, &fx.flow_controller);
    mgr.Bind(owner);

    auto stream = std::dynamic_pointer_cast<SendStream>(mgr.MakeStream(1024, 4, StreamDirection::kSend));
    ASSERT_NE(stream, nullptr);

    uint8_t data[4] = {1, 2, 3, 4};
    EXPECT_GT(stream->Send(data, sizeof(data)), 0);
    EXPECT_EQ(fx.sink.stream_data_ready, 1);
    EXPECT_EQ(fx.sink.stream_closed, 0);
}

// An unbound token behaves as "connection dead": the constructor wires the
// token only after the connection is owned by a shared_ptr, so streams made
// before that would be inert rather than unsafe.
TEST(StreamManagerLifecycleTest, UnboundManagerDropsCallbacksSafely) {
    Fixture fx;

    std::shared_ptr<SendStream> escaped;
    {
        StreamManager mgr(fx.sink, fx.loop, fx.tp, fx.send_manager, nullptr, &fx.flow_controller);
        // No Bind() call: token expired from the start.
        escaped = std::dynamic_pointer_cast<SendStream>(mgr.MakeStream(1024, 8, StreamDirection::kSend));
        ASSERT_NE(escaped, nullptr);
    }

    uint8_t data[4] = {9, 9, 9, 9};
    EXPECT_GT(escaped->Send(data, sizeof(data)), 0);
    EXPECT_EQ(fx.sink.stream_data_ready, 0);
}

}  // namespace
}  // namespace quic
}  // namespace quicx
