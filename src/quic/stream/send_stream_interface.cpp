#include <functional>
#include "quic/stream/send_state_machine.h"
#include "quic/stream/send_stream_interface.h"

namespace quicx {

ISendStream::ISendStream(uint64_t id, bool is_crypto_stream):
    IStream(id),
    _is_crypto_stream(is_crypto_stream) {
    _send_machine = std::shared_ptr<SendStreamStateMachine>();
    //_send_machine->SetStreamCloseCB(std::bind(&IStream::NoticeToClose, this));
}

ISendStream::~ISendStream() {

}

}

