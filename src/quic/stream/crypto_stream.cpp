#include "quic/frame/crypto_frame.h"
#include "quic/stream/crypto_stream.h"

namespace quicx {

CryptoStream::CryptoStream(std::shared_ptr<BlockMemoryPool> alloter, uint64_t id):
    BidirectionStream(alloter, id) {

}

CryptoStream::~CryptoStream() {

}
    
bool CryptoStream::TrySendData(IFrameVisitor* visitor) {
    // TODO check stream state
    for (auto iter = _frame_list.begin(); iter != _frame_list.end();) {
        if (visitor->HandleFrame(*iter)) {
            iter = _frame_list.erase(iter);

        } else {
            return false;
        }
    }

    // make stream frame
    auto frame = std::make_shared<CryptoFrame>();

    // TODO not copy buffer
    uint8_t buf[1000] = {0};
    uint32_t size = _send_buffer->ReadNotMovePt(buf, 1000);
    frame->SetData(buf, size);

    if (!visitor->HandleFrame(frame)) {
        return false;
    }
    _send_buffer->MoveReadPt(size);
    return true;
}

void CryptoStream::OnFrame(std::shared_ptr<IFrame> frame) {
    uint16_t frame_type = frame->GetType();
    if (frame_type == FT_CRYPTO) {
        OnCryptoFrame(frame);
        return;
    }
    BidirectionStream::OnFrame(frame);
}

void CryptoStream::OnCryptoFrame(std::shared_ptr<IFrame> frame) {
    if(!_recv_machine->OnFrame(frame->GetType())) {
        return;
    }

    auto crypto_frame = std::dynamic_pointer_cast<CryptoFrame>(frame);
    _recv_buffer->Write(crypto_frame->GetData(), crypto_frame->GetLength());

    if (_recv_cb) {
        _recv_cb(_recv_buffer, 0);
    }
}

}