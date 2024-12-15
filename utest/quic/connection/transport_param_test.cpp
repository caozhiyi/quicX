#include <gtest/gtest.h>
#include "quic/connection/transport_param.h"
#include "quic/connection/transport_param_config.h"

#include "common/alloter/pool_block.h"
#include "common/buffer/buffer.h"

namespace quicx {
namespace quic {
namespace {

TEST(transport_param_utest, test1) {
    TransportParamConfig config;
    config.original_destination_connection_id_ = "11231";
    config.max_idle_timeout_ = 100;
    config.stateless_reset_token_ = "qwe";
    config.max_udp_payload_size_ = 11;
    config.initial_max_data_ = 12;
    config.initial_max_stream_data_bidi_local_ = 13;
    config.initial_max_stream_data_bidi_remote_ = 14;
    config.initial_max_stream_data_uni_ = 15;
    config.initial_max_streams_bidi_ = 16;
    config.initial_max_streams_uni_ = 17;
    config.ack_delay_exponent_ = 18;
    config.max_ack_delay_ = 19;
    config.disable_active_migration_ = true;
    config.preferred_address_ = "127.0.0.1";
    config.active_connection_id_limit_ = 20;
    config.initial_source_connection_id_ = "127.0.0.1";
    config.retry_source_connection_id_ = "192.168.3.1";

    TransportParam tp1;
    tp1.Init(config);

    auto alloter = common::MakeBlockMemoryPoolPtr(1024, 2);
    std::shared_ptr<common::Buffer> read_buffer = std::make_shared<common::Buffer>(alloter);
    std::shared_ptr<common::Buffer> write_buffer = std::make_shared<common::Buffer>(alloter);

    EXPECT_TRUE(tp1.Encode(write_buffer));

    auto data_span = write_buffer->GetReadSpan();
    auto pos_span = read_buffer->GetWriteSpan();
    memcpy(pos_span.GetStart(), data_span.GetStart(), data_span.GetLength());
    read_buffer->MoveWritePt(data_span.GetLength());

    TransportParam tp2;
    EXPECT_TRUE(tp2.Decode(read_buffer));

    EXPECT_EQ(tp1.GetOriginalDestinationConnectionId(),tp2.GetOriginalDestinationConnectionId());
    EXPECT_EQ(tp1.GetMaxIdleTimeout(),tp2.GetMaxIdleTimeout());
    EXPECT_EQ(tp1.GetStatelessResetToken(),tp2.GetStatelessResetToken());
    EXPECT_EQ(tp1.GetmaxUdpPayloadSize(),tp2.GetmaxUdpPayloadSize());
    EXPECT_EQ(tp1.GetInitialMaxData(),tp2.GetInitialMaxData());
    EXPECT_EQ(tp1.GetInitialMaxStreamDataBidiLocal(),tp2.GetInitialMaxStreamDataBidiLocal());
    EXPECT_EQ(tp1.GetInitialMaxStreamDataBidiRemote(),tp2.GetInitialMaxStreamDataBidiRemote());
    EXPECT_EQ(tp1.GetInitialMaxStreamDataUni(),tp2.GetInitialMaxStreamDataUni());
    EXPECT_EQ(tp1.GetInitialMaxStreamsBidi(),tp2.GetInitialMaxStreamsBidi());
    EXPECT_EQ(tp1.GetInitialMaxStreamsUni(),tp2.GetInitialMaxStreamsUni());
    EXPECT_EQ(tp1.GetackDelayExponent(),tp2.GetackDelayExponent());
    EXPECT_EQ(tp1.GetMaxAckDelay(),tp2.GetMaxAckDelay());
    EXPECT_EQ(tp1.GetDisableActiveMigration(),tp2.GetDisableActiveMigration());
    EXPECT_EQ(tp1.GetPreferredAddress(),tp2.GetPreferredAddress());
    EXPECT_EQ(tp1.GetActiveConnectionIdLimit(),tp2.GetActiveConnectionIdLimit());
    EXPECT_EQ(tp1.GetInitialSourceConnectionId(),tp2.GetInitialSourceConnectionId());
    EXPECT_EQ(tp1.GetRetrySourceConnectionId(),tp2.GetRetrySourceConnectionId());
}


TEST(transport_param_utest, test2) {
    TransportParam tp1;

    auto alloter = common::MakeBlockMemoryPoolPtr(1024, 2);
    std::shared_ptr<common::Buffer> read_buffer = std::make_shared<common::Buffer>(alloter);
    std::shared_ptr<common::Buffer> write_buffer = std::make_shared<common::Buffer>(alloter);

    EXPECT_TRUE(tp1.Encode(write_buffer));

    auto data_span = write_buffer->GetReadSpan();
    auto pos_span = read_buffer->GetWriteSpan();
    memcpy(pos_span.GetStart(), data_span.GetStart(), data_span.GetLength());
    read_buffer->MoveWritePt(data_span.GetLength());

    TransportParam tp2;
    EXPECT_TRUE(tp2.Decode(read_buffer));

    EXPECT_EQ(tp1.GetOriginalDestinationConnectionId(),tp2.GetOriginalDestinationConnectionId());
    EXPECT_EQ(tp1.GetMaxIdleTimeout(),tp2.GetMaxIdleTimeout());
    EXPECT_EQ(tp1.GetStatelessResetToken(),tp2.GetStatelessResetToken());
    EXPECT_EQ(tp1.GetmaxUdpPayloadSize(),tp2.GetmaxUdpPayloadSize());
    EXPECT_EQ(tp1.GetInitialMaxData(),tp2.GetInitialMaxData());
    EXPECT_EQ(tp1.GetInitialMaxStreamDataBidiLocal(),tp2.GetInitialMaxStreamDataBidiLocal());
    EXPECT_EQ(tp1.GetInitialMaxStreamDataBidiRemote(),tp2.GetInitialMaxStreamDataBidiRemote());
    EXPECT_EQ(tp1.GetInitialMaxStreamDataUni(),tp2.GetInitialMaxStreamDataUni());
    EXPECT_EQ(tp1.GetInitialMaxStreamsBidi(),tp2.GetInitialMaxStreamsBidi());
    EXPECT_EQ(tp1.GetInitialMaxStreamsUni(),tp2.GetInitialMaxStreamsUni());
    EXPECT_EQ(tp1.GetackDelayExponent(),tp2.GetackDelayExponent());
    EXPECT_EQ(tp1.GetMaxAckDelay(),tp2.GetMaxAckDelay());
    EXPECT_EQ(tp1.GetDisableActiveMigration(),tp2.GetDisableActiveMigration());
    EXPECT_EQ(tp1.GetPreferredAddress(),tp2.GetPreferredAddress());
    EXPECT_EQ(tp1.GetActiveConnectionIdLimit(),tp2.GetActiveConnectionIdLimit());
    EXPECT_EQ(tp1.GetInitialSourceConnectionId(),tp2.GetInitialSourceConnectionId());
    EXPECT_EQ(tp1.GetRetrySourceConnectionId(),tp2.GetRetrySourceConnectionId());
}

}
}
}