#include "send_stream.h"
#include "recv_stream.h"
#include "bidirection_stream.h"

#include "common/log/log.h"
#include "quic/frame/type.h"
#include "quic/frame/frame_interface.h"

namespace quicx {

BidirectionStream::BidirectionStream() {
    // TODO
    // make recv and send stream
}

BidirectionStream::~BidirectionStream() {

}

int32_t BidirectionStream::Send(uint8_t* data, uint32_t len) {
    return 0;
}

void BidirectionStream::Reset(uint64_t err) {

}

void BidirectionStream::Close() {
    
}

void BidirectionStream::HandleFrame(std::shared_ptr<IFrame> frame) {

}

}