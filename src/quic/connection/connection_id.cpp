#include "quic/connection/connection_id.h"
#include "quic/connection/connection_id_generator.h"

namespace quicx {
namespace quic {

ConnectionID::ConnectionID(): length_(kMaxCidLength), sequence_number_(0), hash_(0) {
    memset(id_, 0, kMaxCidLength);
}

ConnectionID::ConnectionID(const uint8_t* id, uint8_t len, uint64_t sequence_number): length_(len), sequence_number_(sequence_number), hash_(0) {
    memset(id_, 0, kMaxCidLength);
    memcpy(id_, id, len);
}

ConnectionID::ConnectionID(const ConnectionID& other): length_(other.length_), sequence_number_(other.sequence_number_), hash_(other.hash_) {
    memcpy(id_, other.id_, kMaxCidLength);
    hash_ = other.hash_;
    sequence_number_ = other.sequence_number_;
}

ConnectionID::~ConnectionID() {

}

uint64_t ConnectionID::Hash() {
    if (hash_ == 0) {
        hash_ = ConnectionIDGenerator::Instance().Hash(id_, length_);
    }
    return hash_;
}

uint64_t ConnectionID::GetSequenceNumber() const {
    return sequence_number_;
}

const uint8_t* ConnectionID::GetID() const {
    return id_; 
}

uint8_t ConnectionID::GetLength() const {
    return length_;
}

void ConnectionID::SetID(uint8_t* id, uint8_t len) {
    if (len > kMaxCidLength) {
        len = kMaxCidLength;
    }
    memcpy(id_, id, len);
    length_ = len;
    hash_ = 0;
}

void ConnectionID::operator=(const ConnectionID& other) {
    memcpy(id_, other.id_, kMaxCidLength);
    length_ = other.length_;
    sequence_number_ = other.sequence_number_;
    hash_ = other.hash_;
}

bool ConnectionID::operator==(const ConnectionID& other) const {
    return memcmp(id_, other.id_, kMaxCidLength) == 0;
}

bool ConnectionID::operator!=(const ConnectionID& other) const {
    return memcmp(id_, other.id_, kMaxCidLength) != 0;
}

}
}