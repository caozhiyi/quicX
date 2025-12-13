#ifndef QUIC_STREAM_STATE_MACHINE_RECV
#define QUIC_STREAM_STATE_MACHINE_RECV

#include "quic/stream/if_state_machine.h"

namespace quicx {
namespace quic {

/*
Reference: RFC 9000 Section 3.2
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

class StreamStateMachineRecv: public IStreamStateMachine {
public:
    StreamStateMachineRecv(StreamState state = StreamState::kRecv);
    ~StreamStateMachineRecv();

    // current recv frame type
    bool OnFrame(uint16_t frame_type);

    // check if can send this frame type?
    bool CheckCanSendFrame(uint16_t frame_type);
    // can application read all data?
    bool CanAppReadAllData();

    // recv all data from peer
    bool RecvAllData();

    // application read all data
    bool AppReadAllData();

private:
    bool is_reset_received_;
};

}  // namespace quic
}  // namespace quicx

#endif
