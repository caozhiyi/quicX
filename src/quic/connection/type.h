#ifndef QUIC_CONNECTION_TYPE
#define QUIC_CONNECTION_TYPE

#include <cstdint>

namespace quicx {
namespace quic {

// alpn define
static uint8_t __alpn_h3[] = {'h', '3'};
static uint8_t __alpn_transport[] = {'t', 'r', 'a', 'n', 's', 'p', 'o', 'r', 't'};

static const uint16_t __max_cid_length = 20;
static const uint16_t __min_cid_length = 4;

enum SendOperation {
    SO_ALL_SEND_DONE           = 0, // all data has been sent
    SO_SEND_AGAIN_IMMEDIATELY  = 1, // try send data again immediately
    SO_NEXT_PERIOD             = 2, // send data in next period
};

}
}
#endif