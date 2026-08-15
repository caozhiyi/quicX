#include <cstdlib>
#include <cstring>
#include <openssl/rand.h>
#include <openssl/siphash.h>

#include "common/log/log.h"
#include "quic/connection/connection_id_generator.h"

namespace quicx {
namespace quic {

ConnectionIDGenerator::ConnectionIDGenerator() {
    // make key
    // BoringSSL's RAND_bytes aborts internally rather than returning failure, so this
    // branch is defensive. Continuing with a zeroed key would make the CID hash
    // predictable, which is not a state worth limping along in.
    if (RAND_bytes((unsigned char*)sip_hash_key_, sizeof(sip_hash_key_)) != 1) {
        LOG_FATAL("failed to seed the connection id hash key from the cryptographic RNG");
        abort();
    }
}

ConnectionIDGenerator::~ConnectionIDGenerator() {}

bool ConnectionIDGenerator::Generator(uint8_t* cid, uint32_t len) {
    if (RAND_bytes(cid, len) != 1) {
        // Never hand back an all-zero or half-written connection id.
        memset(cid, 0, len);
        LOG_ERROR("failed to generate connection id from the cryptographic RNG");
        return false;
    }
    return true;
}

uint64_t ConnectionIDGenerator::Hash(uint8_t* cid, uint32_t len) {
    return SIPHASH_24(sip_hash_key_, cid, len);
}

}  // namespace quic
}  // namespace quicx