#include <openssl/rand.h>
#include <openssl/siphash.h>
#include "quic/connection/connection_id_generator.h"

namespace quicx {
namespace quic {

ConnectionIDGenerator::ConnectionIDGenerator() {
    // make key
    RAND_bytes((unsigned char*)sip_hash_key_, sizeof(sip_hash_key_));

    // for test
    // sip_hash_key_[0] = 1;
    // sip_hash_key_[1] = 2;
}

ConnectionIDGenerator::~ConnectionIDGenerator() {

}

void ConnectionIDGenerator::Generator(uint8_t* cid, uint32_t len) {
    RAND_bytes(cid, len);
}

uint64_t ConnectionIDGenerator::Hash(uint8_t* cid, uint32_t len) {
    return SIPHASH_24(sip_hash_key_, cid, len);
}

}
}