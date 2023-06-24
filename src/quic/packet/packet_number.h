
#ifndef QUIC_PACKET_PACKET_NUMBER
#define QUIC_PACKET_PACKET_NUMBER

#include "quic/packet/type.h"

namespace quicx {

class PacketNumber {
public:
    PacketNumber();
    ~PacketNumber() {};

    uint64_t NextPakcetNumber(PacketNumberSpace space);
    
    // encode packet number to buffer
    static uint8_t* Encode(uint8_t* pos, uint32_t packet_number_len, uint64_t packet_number);
    // decode packet number from buffer
    static uint8_t* Decode(uint8_t* pos, uint32_t packet_number_len, uint64_t& packet_number);
    // decode except packet number by truncated packet number
    static uint64_t Decode(uint64_t largest_pn, uint64_t truncated_pn, uint64_t truncated_pn_bits);

private:
    PacketNumberSpace _space;
    uint64_t _cur_packet_number[PNS_NUMBER];
};

}

#endif