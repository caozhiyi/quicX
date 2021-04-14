#include "version.h"

namespace quicx {

std::string ParseVersion(char* packet) {
    std::string version;

    uint8_t first_byte = packet[0];
    uint8_t first_byte_bit1 = ((first_byte & 0x80) != 0);
    uint8_t first_byte_bit2 = ((first_byte & 0x40) != 0);
    uint8_t first_byte_bit3 = ((first_byte & 0x20) != 0);
    uint8_t first_byte_bit4 = ((first_byte & 0x10) != 0);
    uint8_t first_byte_bit5 = ((first_byte & 0x08) != 0);
    uint8_t first_byte_bit6 = ((first_byte & 0x04) != 0);
    uint8_t first_byte_bit7 = ((first_byte & 0x02) != 0);
    uint8_t first_byte_bit8 = ((first_byte & 0x01) != 0);
    if (first_byte_bit1) {
        version = std::string(&packet[1], 4);

    } else if (first_byte_bit5 && !first_byte_bit2) {
        if (!first_byte_bit8) {
            throw ("Packet without version");
        }
    if (first_byte_bit5) {
        version = std::string(&packet[9], 4);

    } else {
        version = std::string(&packet[5], 4);
    }

  } else {
        throw("Packet without version");
  }
}

}
