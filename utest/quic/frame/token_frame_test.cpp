#include <gtest/gtest.h>

#include "common/alloter/pool_block.h"
#include "quic/frame/new_token_frame.h"

TEST(new_token_frame_utest, decode1) {
    quicx::NewTokenFrame frame1;
    quicx::NewTokenFrame frame2;

    auto alloter = quicx::MakeBlockMemoryPoolPtr(128, 2);
    std::shared_ptr<quicx::IBufferReadOnly> read_buffer = std::make_shared<quicx::BufferReadOnly>(alloter);
    std::shared_ptr<quicx::IBufferWriteOnly> write_buffer = std::make_shared<quicx::BufferWriteOnly>(alloter);

    char frame_data[64] = "1234567890123456789012345678901234567890";
    frame1.SetToken((uint8_t*)frame_data, strlen(frame_data));

    EXPECT_TRUE(frame1.Encode(write_buffer));

    auto data_piar = write_buffer->GetAllData();
    auto pos_piar = read_buffer->GetReadPair();
    memcpy(pos_piar.first, data_piar.first, data_piar.second - data_piar.first);
    read_buffer->MoveWritePt(data_piar.second - data_piar.first);
    EXPECT_TRUE(frame2.Decode(read_buffer, true));

    EXPECT_EQ(frame1.GetType(), frame2.GetType());

    auto data2 = frame2.GetToken();
    EXPECT_EQ(std::string(frame_data, strlen(frame_data)), std::string((char*)data2, strlen(frame_data)));
}