#include "quic/connection/controler/connection_flow_control.h"
#include <gtest/gtest.h>
#include "common/util/singleton.h"
#include "quic/connection/transport_param.h"
#include "quic/include/type.h"

namespace quicx {
namespace quic {
namespace {

class TransportParamTest: public common::Singleton<TransportParamTest> {
public:
    TransportParamTest() {
        QuicTransportParams tp;
        tp.initial_max_data_ = 10000;
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
    EXPECT_TRUE(flow_control.CheckPeerControlSendDataLimit(can_send_size, frame));
    EXPECT_EQ(can_send_size, 10000);
    EXPECT_TRUE(frame == nullptr);

    flow_control.AddPeerControlSendData(5000);

    frame = nullptr;
    EXPECT_TRUE(flow_control.CheckPeerControlSendDataLimit(can_send_size, frame));
    EXPECT_EQ(can_send_size, 5000);
    EXPECT_TRUE(frame != nullptr);

    flow_control.AddPeerControlSendDataLimit(20000);

    frame = nullptr;
    EXPECT_TRUE(flow_control.CheckPeerControlSendDataLimit(can_send_size, frame));
    EXPECT_EQ(can_send_size, 15000);
    EXPECT_TRUE(frame == nullptr);

    flow_control.AddPeerControlSendData(10000);

    frame = nullptr;
    EXPECT_TRUE(flow_control.CheckPeerControlSendDataLimit(can_send_size, frame));
    EXPECT_EQ(can_send_size, 5000);
    EXPECT_TRUE(frame != nullptr);
}

TEST(connection_control_flow, remote_send_data) {
    ConnectionFlowControl flow_control(StreamIDGenerator::StreamStarter::kClient);
    flow_control.UpdateConfig(TransportParamTest::Instance().GetTransportParam());

    std::shared_ptr<IFrame> frame;
    EXPECT_TRUE(flow_control.CheckControlPeerSendDataLimit(frame));
    EXPECT_TRUE(frame == nullptr);

    flow_control.AddControlPeerSendData(5000);

    frame = nullptr;
    EXPECT_TRUE(flow_control.CheckControlPeerSendDataLimit(frame));
    EXPECT_TRUE(frame != nullptr);

    flow_control.AddControlPeerSendData(20000);

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