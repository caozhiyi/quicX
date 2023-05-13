#include "quic/frame/crypto_frame.h"
#include "quic/stream/crypto_stream.h"

namespace quicx {

CryptoStream::CryptoStream(std::shared_ptr<BlockMemoryPool> alloter, uint64_t id):
    BidirectionStream(alloter, id) {

}

CryptoStream::~CryptoStream() {

}
    
bool CryptoStream::TrySendData(SendDataVisitor& visitior) {
    // TODO check stream state
    for (auto iter = _frame_list.begin(); iter != _frame_list.end();) {
        if (visitior.HandleFrame(*iter)) {
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

    if (!visitior.HandleFrame(frame)) {
        return false;
    }
    _send_buffer->MoveReadPt(size);
    return true;
}

}