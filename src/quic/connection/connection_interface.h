#ifndef QUIC_CONNECTION_CONNECTION_INTERFACE
#define QUIC_CONNECTION_CONNECTION_INTERFACE

#include <memory>
#include "quic/crypto/tls/type.h"
#include "common/network/address.h"
#include "quic/stream/send_stream.h"
#include "quic/packet/packet_interface.h"
#include "quic/crypto/tls/tls_conneciton.h"
#include "quic/stream/bidirection_stream.h"
#include "quic/frame/if_stream_frame.h"

namespace quicx {
namespace quic {

class IConnection;
typedef std::function<void(std::shared_ptr<IConnection>)> ActiveConnectionCB;
typedef std::function<void(uint64_t id_hash)> ConnectionIDCB;
class IConnection {
public:
    IConnection() {}
    virtual ~IConnection() {}

    virtual std::shared_ptr<ISendStream> MakeSendStream() = 0;
    virtual std::shared_ptr<BidirectionStream> MakeBidirectionalStream() = 0;

    virtual void Close(uint64_t error) = 0;

    // try to build a quic message
    virtual bool GenerateSendData(std::shared_ptr<common::IBuffer> buffer) = 0;

    virtual void OnPackets(uint64_t now, std::vector<std::shared_ptr<IPacket>>& packets) = 0;

    virtual uint64_t GetSock() = 0;
    virtual void SetPeerAddress(const common::Address&& addr) = 0;
    virtual common::Address* GetPeerAddress() = 0;

    virtual void SetActiveConnectionCB(ActiveConnectionCB cb) = 0;
protected:
    virtual bool OnInitialPacket(std::shared_ptr<IPacket> packet) = 0;
    virtual bool On0rttPacket(std::shared_ptr<IPacket> packet) = 0;
    virtual bool OnHandshakePacket(std::shared_ptr<IPacket> packet) = 0;
    virtual bool OnRetryPacket(std::shared_ptr<IPacket> packet) = 0;
    virtual bool On1rttPacket(std::shared_ptr<IPacket> packet) = 0;

    virtual bool OnStreamFrame(std::shared_ptr<IFrame> frame) = 0;

    virtual void ActiveSendStream(std::shared_ptr<IStream> stream) = 0;
    virtual void WriteCryptoData(std::shared_ptr<common::IBufferChains> buffer, int32_t err) = 0;
};

}
}

#endif