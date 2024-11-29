#include <functional>
#include "quic/stream/if_send_stream.h"

namespace quicx {
namespace quic {

ISendStream::ISendStream(uint64_t id, bool is_crypto_stream):
    IStream(id),
    is_crypto_stream_(is_crypto_stream) {
    send_machine_ = std::make_shared<StreamStateMachineSend>(std::bind(&ISendStream::ToClose, this));
}

ISendStream::~ISendStream() {

}

}
}

