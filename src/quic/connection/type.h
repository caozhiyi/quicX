#ifndef QUIC_CONNECTION_TYPE
#define QUIC_CONNECTION_TYPE

#include <cstdint>

namespace quicx {

// alpn define
const char* __alpn_transport = "transport";
const char* __alpn_h3 = "h3";

const uint16_t __max_cid_length = 20;
const uint16_t __min_cid_length = 4;
}

#endif