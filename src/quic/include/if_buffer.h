#ifndef QUIC_INCLUDE_IF_BUFFER
#define QUIC_INCLUDE_IF_BUFFER

#include <cstdint>

namespace quicx {
namespace quic {

/*
 buffer interface
*/ 
class IBuffer {
public:
    IBuffer() {}
    virtual ~IBuffer() {}

    // read data from buffer but don't move the read offset
    // return the length of data actually read
    virtual uint32_t ReadNotMovePt(uint8_t* data, uint32_t len) = 0;

    // move read point
    // return the length of the data actually moved
    virtual uint32_t MoveReadPt(int32_t len) = 0;

    // return the length of the data actually read
    virtual uint32_t Read(uint8_t* data, uint32_t len) = 0;

    // return remaining length of readable data
    virtual uint32_t GetDataLength() = 0;

    // return the length of the actual write
    virtual uint32_t Write(uint8_t* data, uint32_t len) = 0;

    // return the length of the data actually move
    virtual uint32_t MoveWritePt(int32_t len) = 0;
};

}
}

#endif