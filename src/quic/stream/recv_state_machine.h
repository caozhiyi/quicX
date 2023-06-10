#ifndef QUIC_STREAM_RECV_STATE_MACHINE
#define QUIC_STREAM_RECV_STATE_MACHINE

#include <string>
#include "quic/stream/state_machine_interface.h"

namespace quicx {

/*
receiving stream states
    o
    | Recv STREAM / STREAM_DATA_BLOCKED / RESET_STREAM
    | Create Bidirectional Stream (Sending)
    | Recv MAX_STREAM_DATA / STOP_SENDING (Bidirectional)
    | Create Higher-Numbered Stream
    v
+-------+
|  Recv | Recv RESET_STREAM
|       |-----------------------.
+-------+                       |
    |                           |  
    | Recv STREAM + FIN         |
    v                           |
+-------+                       |
| Size  |   Recv RESET_STREAM   |
| Known |---------------------->|
+-------+                       |
    |                           |
    |Recv All Data              |
    v                           v
+-------+ Recv RESET_STREAM +-------+
| Data  |--- (optional) --->| Reset |
| Recvd |   Recv All Data   | Recvd |
+-------+<-- (optional) ----+-------+
    |                           |
    | App Read All Data         | App Read RST
    v                           v
+-------+                   +-------+
| Data  |                   | Reset |
| Read  |                   | Read  |
+-------+                   +-------+
*/

enum RecvStreamEvent {
    RSE_RECV_ALL_DATA = 0x01,
    RSE_READ_ALL_DATA = 0x02,
    RSE_READ_RST      = 0x03,
};

class RecvStreamStateMachine: public IStreamStateMachine {
public:
    RecvStreamStateMachine(StreamState s = SS_RECV);
    ~RecvStreamStateMachine();

    // current recv frame type
    bool OnFrame(uint16_t frame_type);

    bool RecvAllData();
};

}

#endif
