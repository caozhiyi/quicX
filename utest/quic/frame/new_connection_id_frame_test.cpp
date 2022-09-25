#include <gtest/gtest.h>

#include "common/alloter/pool_block.h"
#include "quic/frame/new_connection_id_frame.h"

TEST(new_connection_id_frame_utest, decode1) {
    quicx::NewConnectionIDFrame frame1;
    quicx::NewConnectionIDFrame frame2;

    auto alloter = quicx::MakeBlockMemoryPoolPtr(256, 2);
    std::shared_ptr<quicx::IBufferReadOnly> read_buffer = std::make_shared<quicx::BufferReadOnly>(alloter);
    std::shared_ptr<quicx::IBufferWriteOnly> write_buffer = std::make_shared<quicx::BufferWriteOnly>(alloter);

    frame1.SetRetirePriorTo(10086);
    frame1.SetSequenceNumber(2352632);

    char toekn[128] = "123456789012345678901234567890123456789801234567890";
    frame1.SetStatelessResetToken((uint8_t*)toekn);

    frame1.AddConnectionID(1212121);
    frame1.AddConnectionID(1212122);
    frame1.AddConnectionID(1212123);
    frame1.AddConnectionID(1212124);
    frame1.AddConnectionID(1212125);

    EXPECT_TRUE(frame1.Encode(write_buffer));

    auto data_piar = write_buffer->GetAllData();
    auto pos_piar = read_buffer->GetReadPair();
    memcpy(pos_piar.first, data_piar.first, data_piar.second - data_piar.first);
    read_buffer->MoveWritePt(data_piar.second - data_piar.first);
    EXPECT_TRUE(frame2.Decode(read_buffer, true));

    EXPECT_EQ(frame1.GetType(), frame2.GetType());
    EXPECT_EQ(frame1.GetRetirePriorTo(), frame2.GetRetirePriorTo());
    EXPECT_EQ(frame1.GetSequenceNumber(), frame2.GetSequenceNumber());
    EXPECT_EQ(frame1.GetConnectionID().size(), frame2.GetConnectionID().size());
    EXPECT_EQ(std::string((char*)frame1.GetStatelessResetToken(), quicx::__stateless_reset_token_length), 
        std::string((char*)frame2.GetStatelessResetToken(), quicx::__stateless_reset_token_length));
}