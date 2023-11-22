#ifndef QUIC_INCLUDE_SEND_STREAM
#define QUIC_INCLUDE_SEND_STREAM

#include <string>
#include <cstdint>
#include "quic/include/stream.h"

namespace quicx {
namespace quic {

class QuicxSendStream: 
    virtual public QuicxStream {
public:
    QuicxSendStream() {}
    virtual ~QuicxSendStream() {}

    virtual int32_t Send(uint8_t* data, uint32_t len);

    virtual void SetStreamWriteCallBack(stream_write_call_back cb) = 0;
};

}
}

#endif