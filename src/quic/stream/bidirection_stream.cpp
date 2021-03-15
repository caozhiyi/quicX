#include "send_stream.h"
#include "recv_stream.h"
#include "bidirection_stream.h"

#include "common/log/log.h"
#include "quic/frame/type.h"
#include "quic/frame/frame_interface.h"

namespace quicx {

BidirectionStream::BidirectionStream(StreamType type):
    Stream(type) {
    // TODO
    // make recv and send stream
}

BidirectionStream::~BidirectionStream() {

}

int32_t BidirectionStream::Write(std::shared_ptr<Buffer> buffer, uint32_t len) {
    return _send_stream->Write(buffer, len);
}

int32_t BidirectionStream::Write(const std::string &data) {
    return _send_stream->Write(data);
}

int32_t BidirectionStream::Write(char* data, uint32_t len) {
    return _send_stream->Write(data, len);
}

void BidirectionStream::Close() {
    _send_stream->Close();
}

void BidirectionStream::Reset(uint64_t err) {
    _send_stream->Reset(err);
}

void BidirectionStream::SetReadCallBack(StreamReadBack rb) {
    _recv_stream->SetReadCallBack(rb);
}

void BidirectionStream::SetWriteCallBack(StreamWriteBack wb) {
    _send_stream->SetWriteCallBack(wb);
}

void BidirectionStream::SetDataLimit(uint32_t limit) {
    _recv_stream->SetDataLimit(limit);
}

uint32_t BidirectionStream::GetDataLimit() {
    return _recv_stream->GetDataLimit();
}

void BidirectionStream::SetToDataMax(uint32_t to_data_max) {
    _recv_stream->SetToDataMax(to_data_max);
}

uint32_t BidirectionStream::GetToDataMax() {
    return _recv_stream->GetToDataMax();
}

void BidirectionStream::HandleFrame(std::shared_ptr<Frame> frame) {
    uint16_t frame_type = frame->GetType();
    if (frame_type == FT_STREAM ||
        frame_type == FT_STREAM_DATA_BLOCKED ||
        frame_type == FT_RESET_STREAM) {
        
        _recv_stream->HandleFrame(frame);

    } else if (frame_type == FT_MAX_STREAM_DATA || frame_type == FT_STOP_SENDING) {

        _send_stream->HandleFrame(frame);
       
    } else {
        LOG_ERROR("unexcept frame on bidirection stream. frame type:%d", frame_type);
    }
}
}