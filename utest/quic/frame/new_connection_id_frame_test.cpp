#include <gtest/gtest.h>

#include "common/alloter/pool_block.h"
#include "common/buffer/buffer_queue.h"
#include "common/alloter/pool_alloter.h"
#include "quic/frame/new_connection_id_frame.h"

TEST(new_connection_id_frame_utest, decode1) {
    quicx::NewConnectionIDFrame frame1;
    quicx::NewConnectionIDFrame frame2;

    auto IAlloter = std::make_shared<quicx::AlloterWrap>(quicx::MakePoolAlloterPtr());
    auto block = quicx::MakeBlockMemoryPoolPtr(32, 2);
    auto buffer = std::make_shared<quicx::BufferQueue>(block, IAlloter);

    frame1.SetRetirePriorTo(10086);
    frame1.SetSequenceNumber(2352632);

    char toekn[128] = "123456789012345678901234567890123456789801234567890";
    frame1.SetStatelessResetToken(toekn);

    frame1.AddConnectionID(1212121);
    frame1.AddConnectionID(1212122);
    frame1.AddConnectionID(1212123);
    frame1.AddConnectionID(1212124);
    frame1.AddConnectionID(1212125);

    EXPECT_TRUE(frame1.Encode(buffer, IAlloter));
    EXPECT_TRUE(frame2.Decode(buffer, IAlloter, true));

    EXPECT_EQ(frame1.GetType(), frame2.GetType());
    EXPECT_EQ(frame1.GetRetirePriorTo(), frame2.GetRetirePriorTo());
    EXPECT_EQ(frame1.GetSequenceNumber(), frame2.GetSequenceNumber());
    EXPECT_EQ(frame1.GetConnectionID().size(), frame2.GetConnectionID().size());
    EXPECT_EQ(std::string(frame1.GetStatelessResetToken(), quicx::__stateless_reset_token_length), 
        std::string(frame2.GetStatelessResetToken(), quicx::__stateless_reset_token_length));
}