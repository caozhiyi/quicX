#include <gtest/gtest.h>

#include "common/buffer/single_block_buffer.h"
#include "common/buffer/standalone_buffer_chunk.h"

#include "quic/frame/crypto_frame.h"

namespace quicx {
namespace quic {
namespace {

TEST(CryptoFrameTest, codec) {
    CryptoFrame frame1;
    CryptoFrame frame2;

    std::shared_ptr<common::SingleBlockBuffer> read_buffer =
        std::make_shared<common::SingleBlockBuffer>(std::make_shared<common::StandaloneBufferChunk>(128));
    std::shared_ptr<common::SingleBlockBuffer> write_buffer =
        std::make_shared<common::SingleBlockBuffer>(std::make_shared<common::StandaloneBufferChunk>(128));

    char frame_data[64] = "1234567890123456789012345678901234567890";
    std::shared_ptr<common::SingleBlockBuffer> data_buffer =
        std::make_shared<common::SingleBlockBuffer>(std::make_shared<common::StandaloneBufferChunk>(128));
    data_buffer->Write((uint8_t*)frame_data, strlen(frame_data));

    frame1.SetOffset(1042451);
    frame1.SetData(data_buffer->GetSharedReadableSpan());

    EXPECT_TRUE(frame1.Encode(write_buffer));

    auto data_span2 = write_buffer->GetReadableSpan();
    auto pos_span = read_buffer->GetWritableSpan();
    memcpy(pos_span.GetStart(), data_span2.GetStart(), data_span2.GetLength());
    read_buffer->MoveWritePt(data_span2.GetLength());
    EXPECT_TRUE(frame2.Decode(read_buffer, true));

    EXPECT_EQ(frame1.GetType(), frame2.GetType());
    EXPECT_EQ(frame1.GetOffset(), frame2.GetOffset());

    auto data2 = frame2.GetData();
    EXPECT_EQ(std::string(frame_data, strlen(frame_data)), std::string((char*)data2.GetStart(), data2.GetLength()));
}

}  // namespace
}  // namespace quic
}  // namespace quicx