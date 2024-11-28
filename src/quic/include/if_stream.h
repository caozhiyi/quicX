#ifndef QUIC_INCLUDE_IF_STREAM
#define QUIC_INCLUDE_IF_STREAM

#include "quic/include/type.h"

namespace quicx {
namespace quic {

/*
 stream interface
 indicates a quic stream
*/
class IStream {
public:
    IStream() {}
    virtual ~IStream() {}

    virtual void SetUserData(void* user_data) = 0;
    virtual void* GetUserData() = 0;

    virtual StreamType GetType() = 0;
    virtual uint64_t GetStreamID() = 0;

    // close the stream gracefully, the stream will be closed after all data transported.
    virtual void Close() = 0;

    // close the stream immediately, the stream will be closed immediately even if there are some data inflight.
    // error code will be sent to the peer.
    virtual void Reset(int32_t error) = 0;
};

}
}

#endif