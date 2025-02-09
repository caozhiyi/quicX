#include "common/log/log.h"
#include "quic/frame/crypto_frame.h"
#include "quic/stream/crypto_stream.h"
#include "common/buffer/buffer_chains.h"

namespace quicx {
namespace quic {

CryptoStream::CryptoStream(std::shared_ptr<common::BlockMemoryPool> alloter,
    std::function<void(std::shared_ptr<IStream>)> active_send_cb,
    std::function<void(uint64_t stream_id)> stream_close_cb,
    std::function<void(uint64_t error, uint16_t frame_type, const std::string& resion)> connection_close_cb):
    IStream(0, active_send_cb, stream_close_cb, connection_close_cb),
    alloter_(alloter),
    except_offset_(0),
    send_offset_(0) {
    buffer_ = std::make_shared<common::Buffer>(buf_, sizeof(buf_));
}

CryptoStream::~CryptoStream() {

}

IStream::TrySendResult CryptoStream::TrySendData(IFrameVisitor* visitor) {
    // TODO not copy buffer
    TrySendResult ret = TSR_SUCCESS;
    std::shared_ptr<common::IBufferChains> buffer;
    uint8_t level;
    if (send_buffers_[kInitial] && send_buffers_[kInitial]->GetDataLength() > 0) {
        buffer = send_buffers_[kInitial];
        level = kInitial;
    }

    if (send_buffers_[kHandshake] && send_buffers_[kHandshake]->GetDataLength() > 0) {
        if (!buffer) {
            buffer = send_buffers_[kHandshake];
            level = kHandshake;
            send_buffers_[kInitial] = nullptr;

        } else {
            ret = TSR_BREAK;
        }
    }

    if (send_buffers_[kApplication] && send_buffers_[kApplication]->GetDataLength() > 0) {
        if (!buffer) {
            buffer = send_buffers_[kApplication];
            level = kApplication;
            send_buffers_[kHandshake] = nullptr;

        } else {
            ret = TSR_BREAK;
        }
    }
    
    if (!buffer) {
        return ret;
    }
    

    // make crypto frame
    auto frame = std::make_shared<CryptoFrame>();
    frame->SetOffset(send_offset_);
    frame->SetEncryptionLevel(level);

    // TODO not copy buffer
    uint8_t buf[1450] = {0};
    uint32_t size = buffer->ReadNotMovePt(buf, 1450);
    frame->SetData(buf, size);

    if (!visitor->HandleFrame(frame)) {
        ret = TSR_FAILED;
        return ret;
    }

    buffer->MoveReadPt(size);
    send_offset_ += size;
    return ret;
}

// reset the stream
void CryptoStream::Reset(uint32_t error) {
    // do nothing
}

void CryptoStream::Close() {
    // do nothing
}

uint32_t CryptoStream::OnFrame(std::shared_ptr<IFrame> frame) {
    uint16_t frame_type = frame->GetType();
    if (frame_type == FT_CRYPTO) {
        OnCryptoFrame(frame);
        return 0;
    }
    // shouldn't be here
    common::LOG_ERROR("crypto stream recv error frame. type:%d", frame_type);
    return 0;
}

int32_t CryptoStream::Send(uint8_t* data, uint32_t len, uint8_t encryption_level) {
    std::shared_ptr<common::IBufferChains> buffer = send_buffers_[encryption_level];
    if (!buffer) {
        buffer = std::make_shared<common::BufferChains>(alloter_);
        send_buffers_[encryption_level] = buffer;
    }
    int32_t size = buffer->Write(data, len);

    ToSend();
    return size;
}

int32_t CryptoStream::Send(uint8_t* data, uint32_t len) {
    return Send(data, len, GetWaitSendEncryptionLevel());
}

int32_t CryptoStream::Send(std::shared_ptr<common::IBufferRead> buffer) {
    return Send(buffer->GetData(), buffer->GetDataLength(), GetWaitSendEncryptionLevel());
}

uint8_t CryptoStream::GetWaitSendEncryptionLevel() {
    uint8_t level = kApplication;
    if (send_buffers_[kInitial] && send_buffers_[kInitial]->GetDataLength() > 0) {
        level = kInitial;
    } else if (send_buffers_[kHandshake] && send_buffers_[kHandshake]->GetDataLength() > 0) {
        level = kHandshake;
    }
    return level;
}

void CryptoStream::OnCryptoFrame(std::shared_ptr<IFrame> frame) {
    auto crypto_frame = std::dynamic_pointer_cast<CryptoFrame>(frame);
    if (crypto_frame->GetOffset() == except_offset_) {
        buffer_->Write(crypto_frame->GetData(), crypto_frame->GetLength());
        except_offset_ += crypto_frame->GetLength();

        while (true) {
            auto iter = out_order_frame_.find(except_offset_);
            if (iter == out_order_frame_.end()) {
                break;
            }

            crypto_frame = std::dynamic_pointer_cast<CryptoFrame>(iter->second);
            buffer_->Write(crypto_frame->GetData(), crypto_frame->GetLength());
            except_offset_ += crypto_frame->GetLength();
            out_order_frame_.erase(iter);
        }
        
         if (recv_cb_) {
            recv_cb_(buffer_, 0);
        }
    } else {
        out_order_frame_[crypto_frame->GetOffset()] = crypto_frame;
    }
}

}
}