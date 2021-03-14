#include "recv_stream.h"

namespace quicx {

RecvStreamStateMachine::RecvStreamStateMachine(StreamStatus s):
    StreamStateMachine(s) {

}

RecvStreamStateMachine::~RecvStreamStateMachine() {

}

bool RecvStreamStateMachine::OnFrame(uint16_t frame_type) {

}

RecvStream::RecvStream() {

}

RecvStream::~RecvStream() {

}

void RecvStream::Close() {

}

}
