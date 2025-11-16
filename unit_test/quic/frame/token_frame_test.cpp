#include <gtest/gtest.h>

#include "quic/frame/new_token_frame.h"
#include "common/buffer/single_block_buffer.h"
#include "common/buffer/standalone_buffer_chunk.h"

namespace quicx {
namespace quic {
namespace {

TEST(new_token_frame_utest, codec) {
    NewTokenFrame frame1;
    NewTokenFrame frame2;

    std::shared_ptr<common::SingleBlockBuffer> read_buffer = std::make_shared<common::SingleBlockBuffer>(std::make_shared<common::StandaloneBufferChunk>(128));
    std::shared_ptr<common::SingleBlockBuffer> write_buffer = std::make_shared<common::SingleBlockBuffer>(std::make_shared<common::StandaloneBufferChunk>(128));

    char frame_data[64] = "1234567890123456789012345678901234567890";
    frame1.SetToken((uint8_t*)frame_data, strlen(frame_data));

    EXPECT_TRUE(frame1.Encode(write_buffer));

    auto data_span = write_buffer->GetReadableSpan();
    auto pos_span = read_buffer->GetWritableSpan();
    memcpy(pos_span.GetStart(), data_span.GetStart(), data_span.GetLength());
    read_buffer->MoveWritePt(data_span.GetLength());
    EXPECT_TRUE(frame2.Decode(read_buffer, true));

    EXPECT_EQ(frame1.GetType(), frame2.GetType());

    auto data2 = frame2.GetToken();
    EXPECT_EQ(std::string(frame_data, strlen(frame_data)), std::string((char*)data2));
}

}
}
}