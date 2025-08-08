#include "quic/connection/connection_id.h"
#include "quic/connection/connection_id_generator.h"

namespace quicx {
namespace quic {

ConnectionID::ConnectionID(): len_(kMaxCidLength), sequence_number_(0), hash_(0) {
    memset(id_, 0, kMaxCidLength);
}

ConnectionID::ConnectionID(uint8_t* id, uint8_t len, uint64_t sequence_number): len_(len), sequence_number_(sequence_number), hash_(0) {
    memset(id_, 0, kMaxCidLength);
    memcpy(id_, id, len);
}

ConnectionID::ConnectionID(const ConnectionID& other): len_(other.len_), sequence_number_(other.sequence_number_), hash_(other.hash_) {
    memcpy(id_, other.id_, kMaxCidLength);
    hash_ = other.hash_;
    sequence_number_ = other.sequence_number_;
}

ConnectionID::~ConnectionID() {

}

uint64_t ConnectionID::Hash() {
    if (hash_ == 0) {
        hash_ = ConnectionIDGenerator::Instance().Hash(id_, len_);
    }
    return hash_;
}

uint64_t ConnectionID::SequenceNumber() const {
    return sequence_number_;
}

const uint8_t* ConnectionID::ID() const {
    return id_; 
}

uint8_t ConnectionID::Len() const {
    return len_;
}

void ConnectionID::SetID(uint8_t* id, uint8_t len) {
    if (len > kMaxCidLength) {
        len = kMaxCidLength;
    }
    memcpy(id_, id, len);
    len_ = len;
    hash_ = 0;
}

void ConnectionID::operator=(const ConnectionID& other) {
    memcpy(id_, other.id_, kMaxCidLength);
    len_ = other.len_;
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