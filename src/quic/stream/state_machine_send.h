#ifndef QUIC_STREAM_STATE_MACHINE_SEND
#define QUIC_STREAM_STATE_MACHINE_SEND

#include <cstdint>
#include "quic/stream/if_state_machine.h"

namespace quicx {
namespace quic {

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

class StreamStateMachineSend:
    public IStreamStateMachine {
public:
    StreamStateMachineSend(std::function<void()> stream_close_cb, StreamState state = SS_READY);
    ~StreamStateMachineSend();

    // current send frame type
    bool OnFrame(uint16_t frame_type);

    // recv all acks?
    bool AllAckDone();

    // can send stream frame?
    bool CanSendStrameFrame();

    // can send app data?
    bool CanSendAppData();

    // can send data block frame?
    bool CanSendDataBlockFrame();

    // can send reset stream frame?
    bool CanSendResetStreamFrame();
};

}
}

#endif
