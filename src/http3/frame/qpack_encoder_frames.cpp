#include "http3/qpack/util.h"
#include "http3/frame/type.h"
#include "http3/frame/qpack_encoder_frames.h"

namespace quicx {
namespace http3 {

QpackSetCapacityFrame::QpackSetCapacityFrame(uint8_t type):
    IQpackEncoderFrame(type),
    capacity_(0) {
    
}

QpackSetCapacityFrame::QpackSetCapacityFrame(QpackEncoderType type):
    IQpackEncoderFrame(static_cast<uint8_t>(type)),
    capacity_(0) {

}

bool QpackSetCapacityFrame::Encode(std::shared_ptr<common::IBuffer> buffer) {
    // RFC 9204: 001xxxxx with 5-bit prefix in the first byte
    return QpackEncodePrefixedInteger(buffer, kQpackSetCapacityPrefixBits, kQpackSetCapacityFirstByteMask, capacity_);
}

bool QpackSetCapacityFrame::Decode(std::shared_ptr<common::IBuffer> buffer) {
    uint8_t first_byte = 0;
    return QpackDecodePrefixedInteger(buffer, kQpackSetCapacityPrefixBits, first_byte, capacity_);
}

QpackInsertWithNameRefFrame::QpackInsertWithNameRefFrame():
    IQpackEncoderFrame(0) {

}

bool QpackInsertWithNameRefFrame::Encode(std::shared_ptr<common::IBuffer> buffer) {
    // Encoder stream: 1 S i i i i i i (6-bit prefix for index)
    uint8_t mask = kQpackInsertWithNameRefFirstByteBase | (is_static_ ? kQpackInsertWithNameRefStaticBit : 0x00);
    if (!QpackEncodePrefixedInteger(buffer, kQpackInsertWithNameRefPrefixBits, mask, name_index_)) {
        return false;
    }
    return QpackEncodeStringLiteral(value_, buffer, false);
}

bool QpackInsertWithNameRefFrame::Decode(std::shared_ptr<common::IBuffer> buffer) {
    uint8_t first = 0;
    uint64_t idx = 0;
    if (!QpackDecodePrefixedInteger(buffer, kQpackInsertWithNameRefPrefixBits, first, idx)) {
        return false;
    }
    if ((first & kQpackInsertWithNameRefFirstByteBase) == 0) {
        return false;
    }
    is_static_ = (first & kQpackInsertWithNameRefStaticBit) != 0;
    name_index_ = idx;
    return QpackDecodeStringLiteral(buffer, value_);
}

uint32_t QpackInsertWithNameRefFrame::EvaluateEncodeSize() { 
    return 1 + static_cast<uint32_t>(value_.size());
}

void QpackInsertWithNameRefFrame::Set(bool is_static, uint64_t name_index, const std::string& value) { 
    is_static_ = is_static;
    name_index_ = name_index;
    value_ = value;
}

bool QpackInsertWithNameRefFrame::IsStatic() const {
    return is_static_;
}

uint64_t QpackInsertWithNameRefFrame::GetNameIndex() const { 
    return name_index_;
}

const std::string& QpackInsertWithNameRefFrame::GetValue() const {
    return value_;
}


QpackInsertWithoutNameRefFrame::QpackInsertWithoutNameRefFrame():
    IQpackEncoderFrame(0) {

}

bool QpackInsertWithoutNameRefFrame::Encode(std::shared_ptr<common::IBuffer> buffer) {
    // Encoder stream: 01 n n n n n n (6-bit zero) + name/value
    if (!QpackEncodePrefixedInteger(buffer, kQpackInsertWithoutNameRefPrefixBits, kQpackInsertWithoutNameRefFirstByteBase, 0)) {
        return false;
    }
    if (!QpackEncodeStringLiteral(name_, buffer, false)) {
        return false;
    }
    if (!QpackEncodeStringLiteral(value_, buffer, false)) {
        return false;
    }
    return true;
}
bool QpackInsertWithoutNameRefFrame::Decode(std::shared_ptr<common::IBuffer> buffer) {
    uint8_t first = 0;
    uint64_t ignore = 0;
    if (!QpackDecodePrefixedInteger(buffer, kQpackInsertWithoutNameRefPrefixBits, first, ignore)) {
        return false;
    }
    if ((first & 0xC0) != kQpackInsertWithoutNameRefFirstByteBase) {
        return false;
    }
    if (!QpackDecodeStringLiteral(buffer, name_)) {
        return false;
    }
    return QpackDecodeStringLiteral(buffer, value_);
}

uint32_t QpackInsertWithoutNameRefFrame::EvaluateEncodeSize() { 
    return 1 + static_cast<uint32_t>(name_.size() + value_.size());
}

void QpackInsertWithoutNameRefFrame::Set(const std::string& name, const std::string& value) {
    name_ = name; 
    value_ = value;
}

const std::string& QpackInsertWithoutNameRefFrame::GetName() const { 
    return name_;
}

const std::string& QpackInsertWithoutNameRefFrame::GetValue() const {
    return value_;
}

QpackDuplicateFrame::QpackDuplicateFrame():
    IQpackEncoderFrame(0) {

}

bool QpackDuplicateFrame::Encode(std::shared_ptr<common::IBuffer> buffer) {
    // Encoder stream Duplicate: 0001 xxxx (4-bit prefix index)
    return QpackEncodePrefixedInteger(buffer, kQpackDuplicatePrefixBits, kQpackDuplicateFirstByteBase, index_);
}
bool QpackDuplicateFrame::Decode(std::shared_ptr<common::IBuffer> buffer) {
    uint8_t first = 0;
    return QpackDecodePrefixedInteger(buffer, kQpackDuplicatePrefixBits, first, index_);
}
uint32_t QpackDuplicateFrame::EvaluateEncodeSize() { 
    return 1;
}

void QpackDuplicateFrame::Set(uint64_t idx) {
    index_ = idx;
}

uint64_t QpackDuplicateFrame::Get() const {
    return index_;
}

}
}


