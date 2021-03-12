#include <gtest/gtest.h>

#include "common/alloter/pool_block.h"
#include "quic/frame/new_token_frame.h"
#include "common/buffer/buffer_queue.h"
#include "common/alloter/pool_alloter.h"

TEST(new_token_frame_utest, decode1) {
    quicx::NewTokenFrame frame1;
    quicx::NewTokenFrame frame2;

    auto alloter = std::make_shared<quicx::AlloterWrap>(quicx::MakePoolAlloterPtr());
    auto block = quicx::MakeBlockMemoryPoolPtr(32, 2);
    auto buffer = std::make_shared<quicx::BufferQueue>(block, alloter);
    auto data = std::make_shared<quicx::BufferQueue>(block, alloter);

    char frame_data[64] = "1234567890123456789012345678901234567890";
    data->Write(frame_data, sizeof(frame_data));
    frame1.SetToken(data);

    frame1.Encode(buffer, alloter);
    frame2.Decode(buffer, alloter, true);

    EXPECT_EQ(frame1.GetType(), frame2.GetType());

    auto data2 = frame2.GetToken();
    char frame_data2[64] = {0};
    data2->Read(frame_data2, data2->GetCanReadLength());
    EXPECT_EQ(std::string(frame_data), std::string(frame_data2));
}