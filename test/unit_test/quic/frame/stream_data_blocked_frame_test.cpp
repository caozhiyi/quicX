#include <gtest/gtest.h>

#include "common/buffer/single_block_buffer.h"
#include "quic/frame/stream_data_blocked_frame.h"
#include "common/buffer/standalone_buffer_chunk.h"

namespace quicx {
namespace quic {
namespace {

TEST(stream_data_blocked_frame_utest, codec) {
    StreamDataBlockedFrame frame1;
    StreamDataBlockedFrame frame2;

    std::shared_ptr<common::SingleBlockBuffer> read_buffer = std::make_shared<common::SingleBlockBuffer>(std::make_shared<common::StandaloneBufferChunk>(128));
    std::shared_ptr<common::SingleBlockBuffer> write_buffer = std::make_shared<common::SingleBlockBuffer>(std::make_shared<common::StandaloneBufferChunk>(128));

    frame1.SetStreamID(121616546);
    frame1.SetMaximumData(23624236235626);

    EXPECT_TRUE(frame1.Encode(write_buffer));

    auto data_span = write_buffer->GetReadableSpan();
    auto pos_span = read_buffer->GetWritableSpan();
    memcpy(pos_span.GetStart(), data_span.GetStart(), data_span.GetLength());
    read_buffer->MoveWritePt(data_span.GetLength());
    EXPECT_TRUE(frame2.Decode(read_buffer, true));

    EXPECT_EQ(frame1.GetType(), frame2.GetType());
    EXPECT_EQ(frame1.GetStreamID(), frame2.GetStreamID());
    EXPECT_EQ(frame1.GetMaximumData(), frame2.GetMaximumData());
}

}
}
}