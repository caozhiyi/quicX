#include "common/log/log.h"
#include "quic/frame/crypto_frame.h"
#include "quic/stream/crypto_stream.h"
#include "common/buffer/buffer_chains.h"

namespace quicx {

CryptoStream::CryptoStream(std::shared_ptr<BlockMemoryPool> alloter):
    _alloter(alloter),
    _except_offset(0),
    _send_offset(0) {

}

CryptoStream::~CryptoStream() {

}

IStream::TrySendResult CryptoStream::TrySendData(IFrameVisitor* visitor) {
    // TODO not copy buffer
    TrySendResult ret = TSR_SUCCESS;
    std::shared_ptr<IBufferChains> buffer;
    uint8_t level;
    if (_send_buffers[EL_INITIAL] && _send_buffers[EL_INITIAL]->GetDataLength() > 0) {
        buffer = _send_buffers[EL_INITIAL];
        level = EL_INITIAL;
    }

    if (_send_buffers[EL_HANDSHAKE] && _send_buffers[EL_HANDSHAKE]->GetDataLength() > 0) {
        if (!buffer) {
            buffer = _send_buffers[EL_HANDSHAKE];
            level = EL_HANDSHAKE;
            _send_buffers[EL_INITIAL] = nullptr;

        } else {
            ret = TSR_BREAK;
        }
    }

    if (_send_buffers[EL_APPLICATION] && _send_buffers[EL_APPLICATION]->GetDataLength() > 0) {
        if (!buffer) {
            buffer = _send_buffers[EL_APPLICATION];
            level = EL_APPLICATION;
            _send_buffers[EL_HANDSHAKE] = nullptr;

        } else {
            ret = TSR_BREAK;
        }
    }
    
    if (!buffer) {
        return ret;
    }
    

    // make crypto frame
    auto frame = std::make_shared<CryptoFrame>();
    frame->SetOffset(_send_offset);
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
    _send_offset += size;
    return ret;
}

void CryptoStream::Reset(uint64_t err) {
    // do nothing
}

void CryptoStream::Close(uint64_t err) {
    // do nothing
}

uint32_t CryptoStream::OnFrame(std::shared_ptr<IFrame> frame) {
    uint16_t frame_type = frame->GetType();
    if (frame_type == FT_CRYPTO) {
        OnCryptoFrame(frame);
        return 0;
    }
    // shouldn't be here
    LOG_ERROR("crypto stream recv error frame. type:%d", frame_type);
    return 0;
}

int32_t CryptoStream::Send(uint8_t* data, uint32_t len, uint8_t encryption_level) {
    std::shared_ptr<IBufferChains> buffer = _send_buffers[encryption_level];
    if (!buffer) {
        buffer = std::make_shared<BufferChains>(_alloter);
        _send_buffers[encryption_level] = buffer;
    }
    int32_t size = buffer->Write(data, len);

    ActiveToSend();
    return size;
}

int32_t CryptoStream::Send(uint8_t* data, uint32_t len) {
    return 0;
}

uint8_t CryptoStream::GetWaitSendEncryptionLevel() {
    uint8_t level = EL_APPLICATION;
    if (_send_buffers[EL_INITIAL] && _send_buffers[EL_INITIAL]->GetDataLength() > 0) {
        level = EL_INITIAL;
    } else if (_send_buffers[EL_HANDSHAKE] && _send_buffers[EL_HANDSHAKE]->GetDataLength() > 0) {
        level = EL_HANDSHAKE;
    }
    return level;
}

void CryptoStream::OnCryptoFrame(std::shared_ptr<IFrame> frame) {
    if (!_recv_buffer) {
        _recv_buffer = std::make_shared<BufferChains>(_alloter);
    }
    
    auto crypto_frame = std::dynamic_pointer_cast<CryptoFrame>(frame);
    if (crypto_frame->GetOffset() == _except_offset) {
        _recv_buffer->Write(crypto_frame->GetData(), crypto_frame->GetLength());
        _except_offset += crypto_frame->GetLength();

        while (true) {
            auto iter = _out_order_frame.find(_except_offset);
            if (iter == _out_order_frame.end()) {
                break;
            }

            crypto_frame = std::dynamic_pointer_cast<CryptoFrame>(iter->second);
            _recv_buffer->Write(crypto_frame->GetData(), crypto_frame->GetLength());
            _except_offset += crypto_frame->GetLength();
            _out_order_frame.erase(iter);
        }
        
         if (_recv_cb) {
            _recv_cb(_recv_buffer, 0);
        }
    } else {
        _out_order_frame[crypto_frame->GetOffset()] = crypto_frame;
    }
}

}