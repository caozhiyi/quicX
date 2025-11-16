#include <gtest/gtest.h>

#include "common/buffer/single_block_buffer.h"
#include "common/buffer/standalone_buffer_chunk.h"
#include "quic/frame/retire_connection_id_frame.h"

namespace quicx {
namespace quic {
namespace {

TEST(retire_connection_id_frame_utest, codec) {
    RetireConnectionIDFrame frame1;
    RetireConnectionIDFrame frame2;

    std::shared_ptr<common::SingleBlockBuffer> read_buffer = std::make_shared<common::SingleBlockBuffer>(std::make_shared<common::StandaloneBufferChunk>(128));
    std::shared_ptr<common::SingleBlockBuffer> write_buffer = std::make_shared<common::SingleBlockBuffer>(std::make_shared<common::StandaloneBufferChunk>(128));

    frame1.SetSequenceNumber(23624236235626);

    EXPECT_TRUE(frame1.Encode(write_buffer));

    auto data_span = write_buffer->GetReadableSpan();
    auto pos_span = read_buffer->GetWritableSpan();
    memcpy(pos_span.GetStart(), data_span.GetStart(), data_span.GetLength());
    read_buffer->MoveWritePt(data_span.GetLength());
    EXPECT_TRUE(frame2.Decode(read_buffer, true));

    EXPECT_EQ(frame1.GetType(), frame2.GetType());
    EXPECT_EQ(frame1.GetSequenceNumber(), frame2.GetSequenceNumber());
}

}
}
}