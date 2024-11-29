#include "quic/stream/if_recv_stream.h"

namespace quicx {
namespace quic {

IRecvStream::IRecvStream(uint64_t id):
    IStream(id) {
    recv_machine_ = std::make_shared<StreamStateMachineRecv>(std::bind(&IRecvStream::ToClose, this));
}

IRecvStream::~IRecvStream() {

}

}
}

