#include <gtest/gtest.h>
#include "common/util/singleton.h"
#include "quic/connection/transport_param.h"
#include "quic/connection/transport_param_config.h"
#include "quic/connection/controler/flow_control.h"

namespace quicx {
namespace quic {
namespace {

class TransportParamTest:
    public common::Singleton<TransportParamTest> {
public:
    TransportParamTest() {
        TransportParamConfig config;
        config.initial_max_data_ = 10000;
        config.initial_max_stream_data_bidi_local_ = 8;
        config.initial_max_stream_data_bidi_remote_ = 8;
        config.initial_max_stream_data_uni_ = 8;
        config.initial_max_streams_bidi_ = 8;
        config.initial_max_streams_uni_ = 8;
        tp_.Init(config);
    }

    ~TransportParamTest() {}

    TransportParam& GetTransportParam() { return tp_; }
private:
    TransportParam tp_;
};

TEST(connection_control_flow, local_send_data) {
    FlowControl flow_control(StreamIDGenerator::StreamStarter::SS_CLIENT);
    flow_control.InitConfig(TransportParamTest::Instance().GetTransportParam());

    uint32_t can_send_size = 0;
    std::shared_ptr<IFrame> frame;
    EXPECT_TRUE(flow_control.CheckLocalSendDataLimit(can_send_size, frame));
    EXPECT_EQ(can_send_size, 10000);
    EXPECT_TRUE(frame == nullptr);

    flow_control.AddLocalSendData(5000);

    frame = nullptr;
    EXPECT_TRUE(flow_control.CheckLocalSendDataLimit(can_send_size, frame));
    EXPECT_EQ(can_send_size, 5000);
    EXPECT_TRUE(frame != nullptr);

    flow_control.UpdateLocalSendDataLimit(20000);

    frame = nullptr;
    EXPECT_TRUE(flow_control.CheckLocalSendDataLimit(can_send_size, frame));
    EXPECT_EQ(can_send_size, 15000);
    EXPECT_TRUE(frame == nullptr);

    flow_control.AddLocalSendData(10000);

    frame = nullptr;
    EXPECT_TRUE(flow_control.CheckLocalSendDataLimit(can_send_size, frame));
    EXPECT_EQ(can_send_size, 5000);
    EXPECT_TRUE(frame != nullptr);
}

TEST(connection_control_flow, remote_send_data) {
    FlowControl flow_control(StreamIDGenerator::StreamStarter::SS_CLIENT);
    flow_control.InitConfig(TransportParamTest::Instance().GetTransportParam());

    std::shared_ptr<IFrame> frame;
    EXPECT_TRUE(flow_control.CheckRemoteSendDataLimit(frame));
    EXPECT_TRUE(frame == nullptr);

    flow_control.AddRemoteSendData(5000);

    frame = nullptr;
    EXPECT_TRUE(flow_control.CheckRemoteSendDataLimit(frame));
    EXPECT_TRUE(frame != nullptr);

    flow_control.AddRemoteSendData(20000);

    frame = nullptr;
    EXPECT_FALSE(flow_control.CheckRemoteSendDataLimit(frame));
    EXPECT_TRUE(frame == nullptr);
}


TEST(connection_control_flow, local_bidirection_streams) {
    FlowControl flow_control(StreamIDGenerator::StreamStarter::SS_CLIENT);
    flow_control.InitConfig(TransportParamTest::Instance().GetTransportParam());

    uint64_t stream_id = 0;
    std::shared_ptr<IFrame> frame;
    for (size_t i = 1; i <= 4; i++) {
        EXPECT_TRUE(flow_control.CheckLocalBidirectionStreamLimit(stream_id, frame));
        EXPECT_EQ(stream_id, i << 2 | StreamIDGenerator::StreamStarter::SS_CLIENT | StreamIDGenerator::StreamDirection::SD_BIDIRECTIONAL);
        EXPECT_TRUE(frame == nullptr);
    }
    
    EXPECT_TRUE(flow_control.CheckLocalBidirectionStreamLimit(stream_id, frame));
    EXPECT_EQ(stream_id, 5 << 2 | StreamIDGenerator::StreamStarter::SS_CLIENT | StreamIDGenerator::StreamDirection::SD_BIDIRECTIONAL);
    EXPECT_TRUE(frame != nullptr);

    flow_control.UpdateLocalBidirectionStreamLimit(16);

    frame = nullptr;
    EXPECT_TRUE(flow_control.CheckLocalBidirectionStreamLimit(stream_id, frame));
    EXPECT_EQ(stream_id, 6 << 2 | StreamIDGenerator::StreamStarter::SS_CLIENT | StreamIDGenerator::StreamDirection::SD_BIDIRECTIONAL);
    EXPECT_TRUE(frame == nullptr);
}

TEST(connection_control_flow, local_unidirection_streams) {
    FlowControl flow_control(StreamIDGenerator::StreamStarter::SS_CLIENT);
    flow_control.InitConfig(TransportParamTest::Instance().GetTransportParam());

    uint64_t stream_id = 0;
    std::shared_ptr<IFrame> frame;
    for (size_t i = 1; i <= 4; i++) {
        EXPECT_TRUE(flow_control.CheckLocalUnidirectionStreamLimit(stream_id, frame));
        EXPECT_EQ(stream_id, i << 2 | StreamIDGenerator::StreamStarter::SS_CLIENT | StreamIDGenerator::StreamDirection::SD_UNIDIRECTIONAL);
        EXPECT_TRUE(frame == nullptr);
    }
    
    EXPECT_TRUE(flow_control.CheckLocalUnidirectionStreamLimit(stream_id, frame));
    EXPECT_EQ(stream_id, 5 << 2 | StreamIDGenerator::StreamStarter::SS_CLIENT | StreamIDGenerator::StreamDirection::SD_UNIDIRECTIONAL);
    EXPECT_TRUE(frame != nullptr);

    flow_control.UpdateLocalUnidirectionStreamLimit(16);

    frame = nullptr;
    EXPECT_TRUE(flow_control.CheckLocalUnidirectionStreamLimit(stream_id, frame));
    EXPECT_EQ(stream_id, 6 << 2 | StreamIDGenerator::StreamStarter::SS_CLIENT | StreamIDGenerator::StreamDirection::SD_UNIDIRECTIONAL);
    EXPECT_TRUE(frame == nullptr);
}

TEST(connection_control_flow, remote_bidirection_streams) {
    FlowControl flow_control(StreamIDGenerator::StreamStarter::SS_CLIENT);
    flow_control.InitConfig(TransportParamTest::Instance().GetTransportParam());

    StreamIDGenerator generator = StreamIDGenerator(StreamIDGenerator::StreamStarter::SS_SERVER);

    std::shared_ptr<IFrame> frame;
    for (size_t i = 0; i < 4; i++) {
        uint64_t stream_id = generator.NextStreamID(StreamIDGenerator::StreamDirection::SD_BIDIRECTIONAL);
        EXPECT_TRUE(flow_control.CheckRemoteStreamLimit(stream_id, frame));
        EXPECT_TRUE(frame == nullptr);
    }
    
    uint64_t stream_id = generator.NextStreamID(StreamIDGenerator::StreamDirection::SD_BIDIRECTIONAL);
    EXPECT_TRUE(flow_control.CheckRemoteStreamLimit(stream_id, frame));
    EXPECT_TRUE(frame != nullptr);
    
    frame = nullptr;
    stream_id = generator.NextStreamID(StreamIDGenerator::StreamDirection::SD_BIDIRECTIONAL);
    EXPECT_TRUE(flow_control.CheckRemoteStreamLimit(stream_id, frame));
    EXPECT_TRUE(frame == nullptr);
}


TEST(connection_control_flow, remote_unidirection_streams) {
    FlowControl flow_control(StreamIDGenerator::StreamStarter::SS_CLIENT);
    flow_control.InitConfig(TransportParamTest::Instance().GetTransportParam());

    StreamIDGenerator generator = StreamIDGenerator(StreamIDGenerator::StreamStarter::SS_SERVER);

    std::shared_ptr<IFrame> frame;
    for (size_t i = 0; i < 4; i++) {
        uint64_t stream_id = generator.NextStreamID(StreamIDGenerator::StreamDirection::SD_UNIDIRECTIONAL);
        EXPECT_TRUE(flow_control.CheckRemoteStreamLimit(stream_id, frame));
        EXPECT_TRUE(frame == nullptr);
    }
    
    uint64_t stream_id = generator.NextStreamID(StreamIDGenerator::StreamDirection::SD_UNIDIRECTIONAL);
    EXPECT_TRUE(flow_control.CheckRemoteStreamLimit(stream_id, frame));
    EXPECT_TRUE(frame != nullptr);
    
    frame = nullptr;
    stream_id = generator.NextStreamID(StreamIDGenerator::StreamDirection::SD_UNIDIRECTIONAL);
    EXPECT_TRUE(flow_control.CheckRemoteStreamLimit(stream_id, frame));
    EXPECT_TRUE(frame == nullptr);
}

}
}
}