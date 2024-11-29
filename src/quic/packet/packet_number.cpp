#include <cstring>
#include "quic/packet/packet_number.h"

namespace quicx {
namespace quic {

PacketNumber::PacketNumber() {
    memset(cur_packet_number_, 0, sizeof(cur_packet_number_));
}

uint64_t PacketNumber::NextPakcetNumber(PacketNumberSpace space) {
    return ++cur_packet_number_[space];
}

uint32_t PacketNumber::GetPacketNumberLength(uint64_t packet_number) {
    uint32_t len = 0;
    while (packet_number > 0) {
        len++;
        packet_number = packet_number >> 8;
    }
    return len;
}

uint8_t* PacketNumber::Encode(uint8_t* pos, uint32_t packet_number_len, uint64_t packet_number) {
    if (packet_number_len == 4) {
        *pos++ = packet_number >> 24;
    }

    if (packet_number_len >= 3) {
        *pos++ = packet_number >> 16;
    }

    if (packet_number_len >= 2) {
        *pos++ = packet_number >> 8;
    }

    *pos++ = packet_number;
    return pos;
}

uint8_t* PacketNumber::Decode(uint8_t* pos, uint32_t packet_number_len, uint64_t& pakcet_number) {
    for (int i = 0; i < packet_number_len; i++) {
        pakcet_number = ((pakcet_number) << 8u) + (*pos);
        pos++;
    }
    return pos;
}

uint64_t PacketNumber::Decode(uint64_t largest_pn, uint64_t truncated_pn, uint64_t truncated_pn_bits) {
    uint64_t expected_pn = largest_pn + 1;
    uint64_t pn_win = (uint64_t)1 << truncated_pn_bits;
    uint64_t pn_hwin = pn_win >> (uint64_t)1;
    uint64_t pn_mask = pn_win - 1;

    uint64_t candidate_pn = (expected_pn & ~pn_mask) | truncated_pn;

    static const uint64_t __max_number = (uint64_t)1 << 62;
    if (candidate_pn <= expected_pn - pn_hwin && candidate_pn < (__max_number - pn_win)) {
        return candidate_pn + pn_win;
    }

    if (candidate_pn > expected_pn + pn_hwin && candidate_pn > pn_win) {
        return candidate_pn - pn_win;
    }

    return candidate_pn;
}

}
}