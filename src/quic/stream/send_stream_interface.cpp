#include "quic/stream/send_state_machine.h"
#include "quic/stream/send_stream_interface.h"

namespace quicx {

ISendStream::ISendStream(uint64_t id): IStream(id) {
    _send_machine = std::shared_ptr<SendStreamStateMachine>();
}

ISendStream::~ISendStream() {

}

}

