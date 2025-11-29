#ifndef QUIC_STREAM_STATE_MACHINE_RECV
#define QUIC_STREAM_STATE_MACHINE_RECV

#include "quic/stream/if_state_machine.h"

namespace quicx {
namespace quic {

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

class StreamStateMachineRecv: public IStreamStateMachine {
public:
    StreamStateMachineRecv(std::function<void()> stream_close_cb, StreamState state = StreamState::kRecv);
    ~StreamStateMachineRecv();

    // current recv frame type
    bool OnFrame(uint16_t frame_type);

    // can send max stream data frame?
    bool CanSendMaxStrameDataFrame();

    // can send stop sending frame?
    bool CanSendStopSendingFrame();

    // can application read all data?
    bool CanAppReadAllData();

    // recv all data from peer
    bool RecvAllData();

    // application read all data
    bool AppReadAllData();
};

}  // namespace quic
}  // namespace quicx

#endif
