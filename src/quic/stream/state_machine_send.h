#ifndef QUIC_STREAM_STATE_MACHINE_SEND
#define QUIC_STREAM_STATE_MACHINE_SEND

#include <cstdint>
#include "quic/stream/if_state_machine.h"

namespace quicx {
namespace quic {

/*
Reference: RFC 9002 Section 3.1
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

class StreamStateMachineSend: public IStreamStateMachine {
public:
    StreamStateMachineSend(StreamState state = StreamState::kReady);
    ~StreamStateMachineSend();

    // current send frame type
    bool OnFrame(uint16_t frame_type);

    // check if can send this frame type?
    bool CheckCanSendFrame(uint16_t frame_type);

    // recv all acks, and update state
    bool AllAckDone();
};

}  // namespace quic
}  // namespace quicx

#endif