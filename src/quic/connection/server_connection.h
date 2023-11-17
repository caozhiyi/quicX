#ifndef QUIC_CONNECTION_SERVER_CONNECTION
#define QUIC_CONNECTION_SERVER_CONNECTION

#include <memory>
#include <cstdint>
#include "common/timer/timer_interface.h"
#include "quic/connection/base_connection.h"
#include "quic/crypto/tls/tls_server_conneciton.h"

namespace quicx {

class ServerConnection:
    public BaseConnection,
    public TlsServerHandlerInterface {
public:
    ServerConnection(std::shared_ptr<TLSCtx> ctx,
        std::shared_ptr<ITimer> timer,
        ConnectionIDCB add_conn_id_cb,
        ConnectionIDCB retire_conn_id_cb);
    virtual ~ServerConnection();

    virtual void AddRemoteConnectionId(uint8_t* id, uint16_t len);
    // TODO
    // 1. 监听传入的连接
    // 2. 如果支持早期数据，在发送给客户端的TLS恢复ticket中嵌入应用层控制数据
    // 3. 如果支持早期数据，从接收自客户端的恢复ticket中恢复应用层控制数据，并根据该信息接受或拒绝早期数据。
    virtual void Close();

    // set transport param
    void AddTransportParam(TransportParamConfig& tp_config);

    void SSLAlpnSelect(const unsigned char **out, unsigned char *outlen,
        const unsigned char *in, unsigned int inlen, void *arg);

protected:
    virtual bool On0rttPacket(std::shared_ptr<IPacket> packet);
    virtual bool OnRetryPacket(std::shared_ptr<IPacket> packet);

    virtual void WriteCryptoData(std::shared_ptr<IBufferChains> buffer, int32_t err);
private:
    std::shared_ptr<TLSServerConnection> _tls_connection;
};

}

#endif