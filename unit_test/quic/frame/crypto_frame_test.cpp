#include <gtest/gtest.h>

#include "quic/frame/crypto_frame.h"
#include "common/alloter/pool_block.h"
#include "common/buffer/buffer.h"

namespace quicx {
namespace quic {
namespace {

TEST(crypto_frame_utest, codec) {
    CryptoFrame frame1;
    CryptoFrame frame2;

    auto alloter = common::MakeBlockMemoryPoolPtr(128, 2);
    std::shared_ptr<common::Buffer> read_buffer = std::make_shared<common::Buffer>(alloter);
    std::shared_ptr<common::Buffer> write_buffer = std::make_shared<common::Buffer>(alloter);

    char frame_data[64] = "1234567890123456789012345678901234567890";
    frame1.SetOffset(1042451);
    frame1.SetData((uint8_t*)frame_data, strlen(frame_data));

    EXPECT_TRUE(frame1.Encode(write_buffer));

    auto data_span = write_buffer->GetReadSpan();
    auto pos_span = read_buffer->GetWriteSpan();
    memcpy(pos_span.GetStart(), data_span.GetStart(), data_span.GetLength());
    read_buffer->MoveWritePt(data_span.GetLength());
    EXPECT_TRUE(frame2.Decode(read_buffer, true));

    EXPECT_EQ(frame1.GetType(), frame2.GetType());
    EXPECT_EQ(frame1.GetOffset(), frame2.GetOffset());

    auto data2 = frame2.GetData();
    EXPECT_EQ(std::string(frame_data, strlen(frame_data)), std::string((char*)data2, strlen(frame_data)));
}

}
}
}