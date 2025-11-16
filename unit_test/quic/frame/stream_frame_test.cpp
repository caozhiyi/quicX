#include <gtest/gtest.h>

#include "quic/frame/stream_frame.h"
#include "common/buffer/single_block_buffer.h"
#include "common/buffer/standalone_buffer_chunk.h"


namespace quicx {
namespace quic {
namespace {

TEST(stream_frame_utest, codec) {
    StreamFrame frame1;
    StreamFrame frame2;

    std::shared_ptr<common::SingleBlockBuffer> read_buffer = std::make_shared<common::SingleBlockBuffer>(std::make_shared<common::StandaloneBufferChunk>(128));
    std::shared_ptr<common::SingleBlockBuffer> write_buffer = std::make_shared<common::SingleBlockBuffer>(std::make_shared<common::StandaloneBufferChunk>(128));

    char frame_data[64] = "1234567890123456789012345678901234567890";
    std::shared_ptr<common::SingleBlockBuffer> data_buffer = std::make_shared<common::SingleBlockBuffer>(std::make_shared<common::StandaloneBufferChunk>(128));
    data_buffer->Write((uint8_t*)frame_data, strlen(frame_data));

    frame1.SetFin();
    frame1.SetOffset(1042451);
    frame1.SetStreamID(20010);
    frame1.SetData(data_buffer->GetSharedReadableSpan());

    EXPECT_TRUE(frame1.Encode(write_buffer));

    auto data_span = write_buffer->GetReadableSpan();
    auto pos_span = read_buffer->GetWritableSpan();
    memcpy(pos_span.GetStart(), data_span.GetStart(), data_span.GetLength());
    read_buffer->MoveWritePt(data_span.GetLength());
    EXPECT_TRUE(frame2.Decode(read_buffer, true));

    EXPECT_EQ(frame1.GetType(), frame2.GetType());
    EXPECT_TRUE(frame2.HasLength());
    EXPECT_TRUE(frame2.HasOffset());
    EXPECT_EQ(frame1.GetStreamID(), frame2.GetStreamID());
    EXPECT_EQ(frame1.GetOffset(), frame2.GetOffset());

    auto data2 = frame2.GetData();
    EXPECT_EQ(std::string(frame_data, strlen(frame_data)), std::string((char*)data2.GetStart(), data2.GetLength()));
}

}
}
}