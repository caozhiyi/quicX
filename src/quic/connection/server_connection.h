#ifndef QUIC_CONNECTION_SERVER_CONNECTION
#define QUIC_CONNECTION_SERVER_CONNECTION

#include <memory>
#include <cstdint>
#include "common/timer/timer_interface.h"
#include "quic/connection/base_connection.h"
#include "quic/crypto/tls/tls_server_conneciton.h"

namespace quicx {
namespace quic {

class ServerConnection:
    public BaseConnection,
    public TlsServerHandlerInterface {
public:
    ServerConnection(std::shared_ptr<TLSCtx> ctx,
        std::shared_ptr<common::ITimer> timer,
        std::function<void(uint64_t/*cid hash*/, std::shared_ptr<IConnection>)> add_conn_id_cb,
        std::function<void(uint64_t/*cid hash*/)> retire_conn_id_cb);
    virtual ~ServerConnection();

    virtual void AddRemoteConnectionId(uint8_t* id, uint16_t len);

    // set transport param
    void AddTransportParam(TransportParamConfig& tp_config);

    void SSLAlpnSelect(const unsigned char **out, unsigned char *outlen,
        const unsigned char *in, unsigned int inlen, void *arg);

protected:
    virtual bool On0rttPacket(std::shared_ptr<IPacket> packet);
    virtual bool OnRetryPacket(std::shared_ptr<IPacket> packet);

    virtual void WriteCryptoData(std::shared_ptr<common::IBufferChains> buffer, int32_t err);
private:
    std::shared_ptr<TLSServerConnection> tls_connection_;
};

}
}

#endif