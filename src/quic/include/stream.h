#ifndef QUIC_INCLUDE_STREAM
#define QUIC_INCLUDE_STREAM

#include <string>
#include <cstdint>
#include "quic/include/type.h"

namespace quicx {
namespace quic {

class QuicxStream {
public:
    QuicxStream(StreamType st): _type(st) {}
    virtual ~QuicxStream() {}

    virtual void Close() = 0;

    virtual uint64_t GetStreamID() = 0;

    void SetUserData(void* user_data) { _user_data = user_data; }
    void* GetUserData() { return _user_data; }

private:
    StreamType _type;
    void* _user_data;
};

}
}

#endif