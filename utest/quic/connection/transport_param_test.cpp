#include <gtest/gtest.h>
#include "quic/connection/transport_param.h"
#include "quic/connection/transport_param_config.h"

#include "common/alloter/pool_block.h"
#include "common/buffer/buffer.h"

namespace quicx {
namespace {

TEST(transport_param_utest, test1) {
    quicx::TransportParamConfig config;
    config._original_destination_connection_id = "11231";
    config._max_idle_timeout = 100;
    config._stateless_reset_token = "qwe";
    config._max_udp_payload_size = 11;
    config._initial_max_data = 12;
    config._initial_max_stream_data_bidi_local = 13;
    config._initial_max_stream_data_bidi_remote = 14;
    config._initial_max_stream_data_uni = 15;
    config._initial_max_streams_bidi = 16;
    config._initial_max_streams_uni = 17;
    config._ack_delay_exponent = 18;
    config._max_ack_delay = 19;
    config._disable_active_migration = true;
    config._preferred_address = "127.0.0.1";
    config._active_connection_id_limit = 20;
    config._initial_source_connection_id = "127.0.0.1";
    config._retry_source_connection_id = "192.168.3.1";

    quicx::TransportParam tp1;
    tp1.Init(config);

    auto alloter = quicx::MakeBlockMemoryPoolPtr(1024, 2);
    std::shared_ptr<Buffer> read_buffer = std::make_shared<Buffer>(alloter);
    std::shared_ptr<Buffer> write_buffer = std::make_shared<Buffer>(alloter);

    EXPECT_TRUE(tp1.Encode(write_buffer));

    auto data_span = write_buffer->GetReadSpan();
    auto pos_span = read_buffer->GetWriteSpan();
    memcpy(pos_span.GetStart(), data_span.GetStart(), data_span.GetLength());
    read_buffer->MoveWritePt(data_span.GetLength());

    quicx::TransportParam tp2;
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
    quicx::TransportParam tp1;

    auto alloter = quicx::MakeBlockMemoryPoolPtr(1024, 2);
    std::shared_ptr<Buffer> read_buffer = std::make_shared<Buffer>(alloter);
    std::shared_ptr<Buffer> write_buffer = std::make_shared<Buffer>(alloter);

    EXPECT_TRUE(tp1.Encode(write_buffer));

    auto data_span = write_buffer->GetReadSpan();
    auto pos_span = read_buffer->GetWriteSpan();
    memcpy(pos_span.GetStart(), data_span.GetStart(), data_span.GetLength());
    read_buffer->MoveWritePt(data_span.GetLength());

    quicx::TransportParam tp2;
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