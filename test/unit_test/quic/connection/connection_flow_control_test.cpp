#include <gtest/gtest.h>

#include "common/util/singleton.h"

#include "quic/connection/controler/connection_flow_control.h"
#include "quic/config.h"
#include "quic/connection/transport_param.h"
#include "quic/include/type.h"

namespace quicx {
namespace quic {
namespace {

class TransportParamTest: public common::Singleton<TransportParamTest> {
public:
    TransportParamTest() {
        QuicTransportParams tp;
        // Set initial_max_data large enough to avoid threshold triggers in basic tests
        // kDataIncreaseThreshold = 512KB, so use 1MB to be safe
        tp.initial_max_data_ = 1024 * 1024;  // 1MB, larger than kDataIncreaseThreshold (512KB)
        tp.initial_max_stream_data_bidi_local_ = 8;
        tp.initial_max_stream_data_bidi_remote_ = 8;
        tp.initial_max_stream_data_uni_ = 8;
        tp.initial_max_streams_bidi_ = 8;
        tp.initial_max_streams_uni_ = 8;
        tp_.Init(tp);
    }

    ~TransportParamTest() {}

    TransportParam& GetTransportParam() { return tp_; }

private:
    TransportParam tp_;
};

TEST(connection_control_flow, local_send_data) {
    ConnectionFlowControl flow_control(StreamIDGenerator::StreamStarter::kClient);
    flow_control.UpdateConfig(TransportParamTest::Instance().GetTransportParam());

    uint64_t can_send_size = 0;
    std::shared_ptr<IFrame> frame;
    
    // Initial check - should have 1MB available
    EXPECT_TRUE(flow_control.CheckPeerControlSendDataLimit(can_send_size, frame));
    EXPECT_EQ(can_send_size, 1024 * 1024);  // 1MB
    EXPECT_TRUE(frame == nullptr);

    // Send 500KB, remaining = 524KB > 16KB threshold
    flow_control.AddPeerControlSendData(500 * 1024);

    frame = nullptr;
    EXPECT_TRUE(flow_control.CheckPeerControlSendDataLimit(can_send_size, frame));
    EXPECT_EQ(can_send_size, 524 * 1024);  // 524KB
    EXPECT_TRUE(frame == nullptr);  // Still above threshold

    // Peer increases our limit to 2MB
    flow_control.AddPeerControlSendDataLimit(2 * 1024 * 1024);

    frame = nullptr;
    EXPECT_TRUE(flow_control.CheckPeerControlSendDataLimit(can_send_size, frame));
    // Already sent 500KB, new limit 2MB, so remaining = 2MB - 500KB = 1548KB
    EXPECT_EQ(can_send_size, 1548 * 1024);
    EXPECT_TRUE(frame == nullptr);

    // Send almost all remaining data, leaving only 15KB
    // Need to send: 1548KB - 15KB = 1533KB
    flow_control.AddPeerControlSendData(1533 * 1024);

    frame = nullptr;
    EXPECT_TRUE(flow_control.CheckPeerControlSendDataLimit(can_send_size, frame));
    EXPECT_EQ(can_send_size, 15 * 1024);  // 15KB remaining < 16KB threshold
    EXPECT_TRUE(frame != nullptr);  // Below threshold, DATA_BLOCKED frame generated
}

TEST(connection_control_flow, remote_send_data) {
    ConnectionFlowControl flow_control(StreamIDGenerator::StreamStarter::kClient);
    flow_control.UpdateConfig(TransportParamTest::Instance().GetTransportParam());

    std::shared_ptr<IFrame> frame;
    EXPECT_TRUE(flow_control.CheckControlPeerSendDataLimit(frame));
    EXPECT_TRUE(frame == nullptr);

    // Receive data but stay above threshold (1MB - 200KB = 824KB > kDataIncreaseThreshold=512KB)
    flow_control.AddControlPeerSendData(200 * 1024);  // 200KB

    frame = nullptr;
    EXPECT_TRUE(flow_control.CheckControlPeerSendDataLimit(frame));
    EXPECT_TRUE(frame == nullptr);  // Still above threshold, no MAX_DATA frame

    // Receive more data to trigger MAX_DATA (1MB - 600KB = 424KB < kDataIncreaseThreshold=512KB)
    flow_control.AddControlPeerSendData(400 * 1024);  // 400KB, total 600KB, remaining = 448KB < 512KB

    frame = nullptr;
    EXPECT_TRUE(flow_control.CheckControlPeerSendDataLimit(frame));
    EXPECT_TRUE(frame != nullptr);  // Below threshold, MAX_DATA frame generated
    // Note: limit is now increased to 1MB + kDataIncreaseAmount (2MB) = 3MB

    // Exceed the NEW limit should fail (600KB + 3MB > 3MB)
    flow_control.AddControlPeerSendData(3 * 1024 * 1024);  // 3MB, exceeds the new limit

    frame = nullptr;
    EXPECT_FALSE(flow_control.CheckControlPeerSendDataLimit(frame));
    EXPECT_TRUE(frame == nullptr);
}

TEST(connection_control_flow, local_bidirection_streams) {
    ConnectionFlowControl flow_control(StreamIDGenerator::StreamStarter::kClient);
    flow_control.UpdateConfig(TransportParamTest::Instance().GetTransportParam());

    uint64_t stream_id = 0;
    std::shared_ptr<IFrame> frame;
    for (size_t i = 0; i <= 4; i++) {
        EXPECT_TRUE(flow_control.CheckPeerControlBidirectionStreamLimit(stream_id, frame));
        EXPECT_EQ(stream_id,
            i << 2 | StreamIDGenerator::StreamStarter::kClient | StreamIDGenerator::StreamDirection::kBidirectional);
        EXPECT_TRUE(frame == nullptr);
    }

    EXPECT_TRUE(flow_control.CheckPeerControlBidirectionStreamLimit(stream_id, frame));
    EXPECT_EQ(stream_id,
        5 << 2 | StreamIDGenerator::StreamStarter::kClient | StreamIDGenerator::StreamDirection::kBidirectional);
    EXPECT_TRUE(frame != nullptr);

    flow_control.AddPeerControlBidirectionStreamLimit(16);

    frame = nullptr;
    EXPECT_TRUE(flow_control.CheckPeerControlBidirectionStreamLimit(stream_id, frame));
    EXPECT_EQ(stream_id,
        6 << 2 | StreamIDGenerator::StreamStarter::kClient | StreamIDGenerator::StreamDirection::kBidirectional);
    EXPECT_TRUE(frame == nullptr);
}

TEST(connection_control_flow, local_unidirection_streams) {
    ConnectionFlowControl flow_control(StreamIDGenerator::StreamStarter::kClient);
    flow_control.UpdateConfig(TransportParamTest::Instance().GetTransportParam());

    uint64_t stream_id = 0;
    std::shared_ptr<IFrame> frame;
    for (size_t i = 0; i <= 4; i++) {
        EXPECT_TRUE(flow_control.CheckPeerControlUnidirectionStreamLimit(stream_id, frame));
        EXPECT_EQ(stream_id,
            i << 2 | StreamIDGenerator::StreamStarter::kClient | StreamIDGenerator::StreamDirection::kUnidirectional);
        EXPECT_TRUE(frame == nullptr);
    }

    EXPECT_TRUE(flow_control.CheckPeerControlUnidirectionStreamLimit(stream_id, frame));
    EXPECT_EQ(stream_id,
        5 << 2 | StreamIDGenerator::StreamStarter::kClient | StreamIDGenerator::StreamDirection::kUnidirectional);
    EXPECT_TRUE(frame != nullptr);

    flow_control.AddPeerControlUnidirectionStreamLimit(16);

    frame = nullptr;
    EXPECT_TRUE(flow_control.CheckPeerControlUnidirectionStreamLimit(stream_id, frame));
    EXPECT_EQ(stream_id,
        6 << 2 | StreamIDGenerator::StreamStarter::kClient | StreamIDGenerator::StreamDirection::kUnidirectional);
    EXPECT_TRUE(frame == nullptr);
}

TEST(connection_control_flow, remote_bidirection_streams) {
    ConnectionFlowControl flow_control(StreamIDGenerator::StreamStarter::kClient);
    flow_control.UpdateConfig(TransportParamTest::Instance().GetTransportParam());

    StreamIDGenerator generator = StreamIDGenerator(StreamIDGenerator::StreamStarter::kServer);

    std::shared_ptr<IFrame> frame;
    for (size_t i = 0; i < 5; i++) {
        uint64_t stream_id = generator.NextStreamID(StreamIDGenerator::StreamDirection::kBidirectional);
        EXPECT_TRUE(flow_control.CheckControlPeerStreamLimit(stream_id, frame));
        EXPECT_TRUE(frame == nullptr);
    }

    uint64_t stream_id = generator.NextStreamID(StreamIDGenerator::StreamDirection::kBidirectional);
    EXPECT_TRUE(flow_control.CheckControlPeerStreamLimit(stream_id, frame));
    EXPECT_TRUE(frame != nullptr);

    frame = nullptr;
    stream_id = generator.NextStreamID(StreamIDGenerator::StreamDirection::kBidirectional);
    EXPECT_TRUE(flow_control.CheckControlPeerStreamLimit(stream_id, frame));
    EXPECT_TRUE(frame == nullptr);
}

TEST(connection_control_flow, remote_unidirection_streams) {
    ConnectionFlowControl flow_control(StreamIDGenerator::StreamStarter::kClient);
    flow_control.UpdateConfig(TransportParamTest::Instance().GetTransportParam());

    StreamIDGenerator generator = StreamIDGenerator(StreamIDGenerator::StreamStarter::kServer);

    std::shared_ptr<IFrame> frame;
    for (size_t i = 0; i < 5; i++) {
        uint64_t stream_id = generator.NextStreamID(StreamIDGenerator::StreamDirection::kUnidirectional);
        EXPECT_TRUE(flow_control.CheckControlPeerStreamLimit(stream_id, frame));
        EXPECT_TRUE(frame == nullptr);
    }

    uint64_t stream_id = generator.NextStreamID(StreamIDGenerator::StreamDirection::kUnidirectional);
    EXPECT_TRUE(flow_control.CheckControlPeerStreamLimit(stream_id, frame));
    EXPECT_TRUE(frame != nullptr);

    frame = nullptr;
    stream_id = generator.NextStreamID(StreamIDGenerator::StreamDirection::kUnidirectional);
    EXPECT_TRUE(flow_control.CheckControlPeerStreamLimit(stream_id, frame));
    EXPECT_TRUE(frame == nullptr);
}

}  // namespace
}  // namespace quic
}  // namespace quicx