#ifndef QUIC_STREAM_RECV_STREAM
#define QUIC_STREAM_RECV_STREAM

#include <string>
#include "stream_interface.h"
#include "stream_state_machine_interface.h"

namespace quicx {

class RecvStreamStateMachine: public StreamStateMachine {
public:
    RecvStreamStateMachine(StreamStatus s = SS_RECV);
    ~RecvStreamStateMachine();

    bool OnFrame(uint16_t frame_type);
};


class RecvStream: public Stream {
public:
    RecvStream();
    virtual ~RecvStream();

    virtual void Close() = 0;

    virtual void SetReadCallBack(StreamWriteBack wb) {}

private:

};

}

#endif
