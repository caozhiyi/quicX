#ifndef QUIC_STREAM_RECV_STATE_MACHINE
#define QUIC_STREAM_RECV_STATE_MACHINE

#include <string>
#include "quic/stream/state_machine_interface.h"

namespace quicx {

enum RecvStreamEvent {
    RSE_RECV_ALL_DATA = 0x01,
    RSE_READ_ALL_DATA = 0x02,
    RSE_READ_RST      = 0x03,
};

class RecvStreamStateMachine: public IStreamStateMachine {
public:
    RecvStreamStateMachine(StreamState s = SS_RECV);
    ~RecvStreamStateMachine();

    bool OnFrame(uint16_t frame_type);

    bool OnEvent(RecvStreamEvent event);
};

}

#endif
