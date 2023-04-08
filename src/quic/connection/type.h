#ifndef QUIC_CONNECTION_TYPE
#define QUIC_CONNECTION_TYPE

#include <cstdint>

namespace quicx {

enum AlpnType {
    AT_HTTP3     = 1,
    AT_TRANSPORT = 2,
};

// alpn define
static uint8_t __alpn_h3[] = {'h', '3'};
static uint8_t __alpn_transport[] = {'t', 'r', 'a', 'n', 's', 'p', 'o', 'r', 't'};

static const uint16_t __max_cid_length = 20;
static const uint16_t __min_cid_length = 4;
}

#endif