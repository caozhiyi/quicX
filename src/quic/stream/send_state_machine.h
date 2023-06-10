#ifndef QUIC_STREAM_SEND_STATE_MACHINE
#define QUIC_STREAM_SEND_STATE_MACHINE

#include <cstdint>
#include "quic/stream/state_machine_interface.h"

namespace quicx {

/*
sending stream states
    o    
    | Create Stream (Sending)    
    | Peer Creates Bidirectional Stream    
    v    
+-------+    
| Ready | Send RESET_STREAM    
|       |-----------------------.    
+-------+                       |    
    |                           |       
    | Send STREAM /             |    
    | STREAM_DATA_BLOCKED       |    
    |                           |    
    | Peer Creates              |    
    | Bidirectional Stream      |    
    v                           |    
+-------+                       |    
| Send  | Send RESET_STREAM     |    
|       |---------------------->|    
+-------+                       |    
    |                           |    
    | Send STREAM + FIN         |    
    v                           v    
+-------+                   +-------+
| Data  | Send RESET_STREAM | Reset |
| Sent  |------------------>| Sent  |
+-------+                   +-------+
    |                           |
    | Recv All ACKs             | Recv ACK
    v                           v
+-------+                   +-------+
| Data  |                   | Reset |
| Recvd |                   | Recvd |
+-------+                   +-------+
*/

class SendStreamStateMachine:
    public IStreamStateMachine {
public:
    SendStreamStateMachine(StreamState s = SS_READY);
    ~SendStreamStateMachine();

    // current send frame type
    bool OnFrame(uint16_t frame_type);

    // recv all acks?
    bool AllAckDone();

    // check can send?
    bool CanSendData();
};

}

#endif
