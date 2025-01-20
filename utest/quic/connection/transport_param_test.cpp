#include <gtest/gtest.h>
#include "common/buffer/buffer.h"
#include "common/alloter/pool_block.h"
#include "quic/connection/transport_param.h"

namespace quicx {
namespace quic {
namespace {

TEST(transport_param_utest, test1) {
    TransportParam tp1;
    tp1.Init(DEFAULT_QUIC_TRANSPORT_PARAMS);

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