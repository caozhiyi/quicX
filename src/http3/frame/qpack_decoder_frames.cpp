#include "http3/qpack/util.h"
#include "http3/frame/type.h"
#include "http3/frame/qpack_decoder_frames.h"

namespace quicx {
namespace http3 {

bool DecodeQpackDecoderFrames(std::shared_ptr<common::IBufferRead> buffer, std::vector<std::shared_ptr<IQpackDecoderFrame>>& frames) {
    if (!buffer) {
        return false;
    }
    while (buffer->GetDataLength() > 0) {
        uint8_t first = 0;
        if (buffer->ReadNotMovePt(&first, 1) != 1) return false;

        std::shared_ptr<IQpackDecoderFrame> frame;
        // Dispatch by bit-pattern of the first byte
        if ((first & 0x80) == kQpackDecSectionAckFirstByteMask) {
            // 1xxxxxxx -> Section Acknowledgement
            frame = std::make_shared<QpackSectionAckFrame>();
        } else if ((first & 0xC0) == kQpackDecStreamCancelFirstByteMask) {
            // 01xxxxxx -> Stream Cancellation
            frame = std::make_shared<QpackStreamCancellationFrame>();
        } else if ((first & 0xC0) == kQpackDecInsertCountIncFirstByteMask) {
            // 00xxxxxx -> Insert Count Increment
            frame = std::make_shared<QpackInsertCountIncrementFrame>();
        } else {
            return false;
        }

        if (!frame->Decode(buffer)) {
            return false;
        }
        frames.push_back(std::move(frame));
    }
    return true;
}

QpackSectionAckFrame::QpackSectionAckFrame():
    IQpackDecoderFrame() {

}

bool QpackSectionAckFrame::Encode(std::shared_ptr<common::IBufferWrite> buffer) {
    // 1xxxxxxx with 7-bit prefix
    return QpackEncodePrefixedInteger(buffer, kQpackDecSectionAckPrefixBits, kQpackDecSectionAckFirstByteMask, stream_id_)
        && QpackEncodePrefixedInteger(buffer, kQpackDecoderVarintPrefixBits, kQpackDecoderVarintFirstByteMask, section_number_);
}

bool QpackSectionAckFrame::Decode(std::shared_ptr<common::IBufferRead> buffer) {
    uint8_t fb1 = 0;
    if (!QpackDecodePrefixedInteger(buffer, kQpackDecSectionAckPrefixBits, fb1, stream_id_)) {
        return false;
    }
    // Next field is a full-varint (8-bit prefix)
    uint8_t fb2 = 0;
    if (!QpackDecodePrefixedInteger(buffer, kQpackDecoderVarintPrefixBits, fb2, section_number_)) {
        return false;
    }
    return true;
}

uint32_t QpackSectionAckFrame::EvaluateEncodeSize() { 
    return 1 + 2;
}

uint64_t QpackSectionAckFrame::GetStreamId() const { 
    return stream_id_;
}

uint64_t QpackSectionAckFrame::GetSectionNumber() const { 
    return section_number_;
}

void QpackSectionAckFrame::Set(uint64_t sid, uint64_t sec) { 
    stream_id_ = sid; 
    section_number_ = sec;
}


QpackStreamCancellationFrame::QpackStreamCancellationFrame():
    IQpackDecoderFrame() {

}

bool QpackStreamCancellationFrame::Encode(std::shared_ptr<common::IBufferWrite> buffer) {
    // 01xxxxxx with 6-bit prefix for stream_id
    return QpackEncodePrefixedInteger(buffer, kQpackDecStreamCancelPrefixBits, kQpackDecStreamCancelFirstByteMask, stream_id_)
        && QpackEncodePrefixedInteger(buffer, kQpackDecoderVarintPrefixBits, kQpackDecoderVarintFirstByteMask, section_number_);
}

bool QpackStreamCancellationFrame::Decode(std::shared_ptr<common::IBufferRead> buffer) {
    uint8_t fb1 = 0;
    if (!QpackDecodePrefixedInteger(buffer, kQpackDecStreamCancelPrefixBits, fb1, stream_id_)) {
        return false;
    }
    uint8_t fb2 = 0;
    if (!QpackDecodePrefixedInteger(buffer, kQpackDecoderVarintPrefixBits, fb2, section_number_)) {
        return false;
    }
    return true;
}

uint32_t QpackStreamCancellationFrame::EvaluateEncodeSize() { 
    return 1;
}

uint64_t QpackStreamCancellationFrame::GetStreamId() const { 
    return stream_id_;
}

uint64_t QpackStreamCancellationFrame::GetSectionNumber() const { 
    return section_number_;
}

void QpackStreamCancellationFrame::Set(uint64_t sid, uint64_t sec) {
    stream_id_ = sid;
    section_number_ = sec;
}


QpackInsertCountIncrementFrame::QpackInsertCountIncrementFrame():
    IQpackDecoderFrame() {

}

bool QpackInsertCountIncrementFrame::Encode(std::shared_ptr<common::IBufferWrite> buffer) {
    // 00xxxxxx with 6-bit prefix for delta
    return QpackEncodePrefixedInteger(buffer, kQpackDecInsertCountIncPrefixBits, kQpackDecInsertCountIncFirstByteMask, delta_);
}

bool QpackInsertCountIncrementFrame::Decode(std::shared_ptr<common::IBufferRead> buffer) {
    uint8_t fb = 0;
    return QpackDecodePrefixedInteger(buffer, kQpackDecInsertCountIncPrefixBits, fb, delta_);
}

uint32_t QpackInsertCountIncrementFrame::EvaluateEncodeSize() {
    return 1 + 1;
}

uint64_t QpackInsertCountIncrementFrame::GetDelta() const { 
    return delta_;
}

void QpackInsertCountIncrementFrame::Set(uint64_t d) { 
    delta_ = d;
}

}
}


