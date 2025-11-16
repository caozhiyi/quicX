#include <gtest/gtest.h>

#include "common/buffer/buffer_read_view.h"
#include "quic/connection/transport_param.h"
#include "common/buffer/buffer_write_view.h"

namespace quicx {
namespace quic {
namespace {

TEST(transport_param_utest, test1) {
    TransportParam tp1;
    tp1.Init(DEFAULT_QUIC_TRANSPORT_PARAMS);

    uint8_t buf[1024] = {0};
    common::BufferWriteView write_buffer(buf, buf + 1024);

    EXPECT_TRUE(tp1.Encode(write_buffer));

    // After encoding, create read_buffer with the actual written length
    uint32_t written_len = write_buffer.GetDataLength();
    common::BufferReadView read_buffer(buf, written_len);

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

    uint8_t buf[1024] = {0};
    common::BufferReadView read_buffer(buf, buf + 1024);
    common::BufferWriteView write_buffer(buf, buf + 1024);

    EXPECT_TRUE(tp1.Encode(write_buffer));

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