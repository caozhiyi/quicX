#ifndef QUIC_CONNECTION_IF_CONNECTION
#define QUIC_CONNECTION_IF_CONNECTION

#include <memory>
#include "quic/crypto/tls/type.h"
#include "quic/packet/if_packet.h"
#include "common/network/address.h"
#include "quic/stream/send_stream.h"
#include "quic/frame/if_stream_frame.h"
#include "quic/crypto/tls/tls_conneciton.h"
#include "quic/stream/bidirection_stream.h"

namespace quicx {
namespace quic {

class IConnection {
public:
    IConnection() {}
    virtual ~IConnection() {}

    virtual uint64_t GetConnectionIDHash() = 0;

    // create a new send stream
    virtual std::shared_ptr<ISendStream> MakeSendStream() = 0;
    // create a new bidirectional stream
    virtual std::shared_ptr<BidirectionStream> MakeBidirectionalStream() = 0;

    virtual void Close(uint64_t error) = 0;

    // try to build a quic message
    virtual bool GenerateSendData(std::shared_ptr<common::IBuffer> buffer) = 0;

    virtual void OnPackets(uint64_t now, std::vector<std::shared_ptr<IPacket>>& packets) = 0;

    virtual uint64_t GetSock() = 0;
    virtual void SetPeerAddress(const common::Address&& addr) = 0;
    virtual const common::Address GetPeerAddress() = 0;

    virtual void SetActiveConnectionCB(std::function<void(std::shared_ptr<IConnection>)> cb) = 0;
};

}
}

#endif