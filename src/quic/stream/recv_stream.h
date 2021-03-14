#ifndef QUIC_STREAM_RECV_STREAM
#define QUIC_STREAM_RECV_STREAM

#include <string>
#include "stream_interface.h"

namespace quicx {

class RecvStream: public Stream {
public:
    RecvStream();
    virtual ~RecvStream();

    virtual void Close() = 0;

    virtual void SetReadCallBack(StreamWriteBack wb) = 0;
};

}

#endif
