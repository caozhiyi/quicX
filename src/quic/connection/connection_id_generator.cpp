#include <openssl/rand.h>
#include <openssl/siphash.h>
#include "quic/connection/connection_id_generator.h"

namespace quicx {

ConnectionIDGenerator::ConnectionIDGenerator() {
    // make key
    Generator(_sip_hash_key, sizeof(_sip_hash_key));
}

ConnectionIDGenerator::~ConnectionIDGenerator() {

}

void ConnectionIDGenerator::Generator(void* cid, uint32_t len) {
    RAND_bytes(reinterpret_cast<uint8_t*>(cid), len);
}

uint64_t ConnectionIDGenerator::Hash(uint8_t* cid, uint16_t len) {
    return SIPHASH_24(_sip_hash_key, cid, len);
}

}