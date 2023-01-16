#include <gtest/gtest.h>

#include "quic/frame/crypto_frame.h"
#include "common/alloter/pool_block.h"
#include "common/buffer/buffer_read_write.h"

namespace quicx {
namespace {

TEST(crypto_frame_utest, decode1) {
    quicx::CryptoFrame frame1;
    quicx::CryptoFrame frame2;

    auto alloter = quicx::MakeBlockMemoryPoolPtr(128, 2);
    std::shared_ptr<BufferReadWrite> read_buffer = std::make_shared<BufferReadWrite>(alloter);
    std::shared_ptr<BufferReadWrite> write_buffer = std::make_shared<BufferReadWrite>(alloter);

    char frame_data[64] = "1234567890123456789012345678901234567890";
    frame1.SetOffset(1042451);
    frame1.SetData((uint8_t*)frame_data, strlen(frame_data));

    EXPECT_TRUE(frame1.Encode(write_buffer));

    auto data_piar = write_buffer->GetReadPair();
    auto pos_piar = read_buffer->GetWritePair();
    memcpy(pos_piar.first, data_piar.first, data_piar.second - data_piar.first);
    read_buffer->MoveWritePt(data_piar.second - data_piar.first);
    EXPECT_TRUE(frame2.Decode(read_buffer, true));

    EXPECT_EQ(frame1.GetType(), frame2.GetType());
    EXPECT_EQ(frame1.GetOffset(), frame2.GetOffset());

    auto data2 = frame2.GetData();
    EXPECT_EQ(std::string(frame_data, strlen(frame_data)), std::string((char*)data2, strlen(frame_data)));
}


TEST(crypto_frame_utest, decod2) {
    quicx::CryptoFrame frame1;
    quicx::CryptoFrame frame2;

    auto alloter = quicx::MakeBlockMemoryPoolPtr(128, 2);
    std::shared_ptr<BufferReadWrite> read_buffer = std::make_shared<BufferReadWrite>(alloter);
    std::shared_ptr<BufferReadWrite> write_buffer = std::make_shared<BufferReadWrite>(alloter);

    char frame_data[64] = "";
    frame1.SetOffset(1042451);
    frame1.SetData((uint8_t*)frame_data, strlen(frame_data));

    EXPECT_TRUE(frame1.Encode(write_buffer));

    auto data_piar = write_buffer->GetReadPair();
    auto pos_piar = read_buffer->GetWritePair();
    memcpy(pos_piar.first, data_piar.first, data_piar.second - data_piar.first);
    read_buffer->MoveWritePt(data_piar.second - data_piar.first);
    EXPECT_TRUE(frame2.Decode(read_buffer, true));

    EXPECT_EQ(frame1.GetType(), frame2.GetType());
    EXPECT_EQ(frame1.GetOffset(), frame2.GetOffset());

    auto data2 = frame2.GetData();
    EXPECT_EQ(std::string(frame_data, strlen(frame_data)), std::string((char*)data2, strlen(frame_data)));
}

}
}