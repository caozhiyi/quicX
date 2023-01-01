#ifndef QUIC_STREAM_SEND_STATE_MACHINE
#define QUIC_STREAM_SEND_STATE_MACHINE

#include <cstdint>
#include "quic/stream/state_machine_interface.h"

namespace quicx {

class SendStreamStateMachine: public IStreamStateMachine {
public:
    SendStreamStateMachine(StreamState s = SS_READY);
    ~SendStreamStateMachine();

    bool OnFrame(uint16_t frame_type);

    bool RecvAllAck();
};

}

#endif
