#include "common/buffer/buffer.h"
#include "quic/frame/crypto_frame.h"
#include "quic/stream/fix_buffer_frame_visitor.h"


namespace quicx {
namespace quic {

FixBufferFrameVisitor::FixBufferFrameVisitor(uint32_t size):
    encryption_level_(kApplication),
    cur_data_offset_(0),
    limit_data_offset_(0) {
    cache_ = new uint8_t[size];
    buffer_ = std::make_shared<common::Buffer>(cache_, cache_ + size);
}

FixBufferFrameVisitor::~FixBufferFrameVisitor() {
    delete[] cache_;
}

bool FixBufferFrameVisitor::HandleFrame(std::shared_ptr<IFrame> frame) {
    if (frame->GetType() == FT_CRYPTO) {
        auto crypto_frame = std::dynamic_pointer_cast<CryptoFrame>(frame);
        encryption_level_ = crypto_frame->GetEncryptionLevel();
    }

    return frame->Encode(buffer_);
}

}
}