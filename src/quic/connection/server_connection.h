#ifndef QUIC_CONNECTION_SERVER_CONNECTION
#define QUIC_CONNECTION_SERVER_CONNECTION

#include <memory>
#include <cstdint>
#include "quic/connection/connection_interface.h"
#include "quic/crypto/tls/tls_server_conneciton.h"

namespace quicx {

class ServerConnection:
    public IConnection,
    public TlsServerHandlerInterface,
    public std::enable_shared_from_this<ServerConnection> {
public:
    ServerConnection(std::shared_ptr<TLSCtx> ctx);
    virtual ~ServerConnection();
    // TODO
    // 1. 监听传入的连接
    // 2. 如果支持早期数据，在发送给客户端的TLS恢复ticket中嵌入应用层控制数据
    // 3. 如果支持早期数据，从接收自客户端的恢复ticket中恢复应用层控制数据，并根据该信息接受或拒绝早期数据。
    virtual void Close();

    void SSLAlpnSelect(const unsigned char **out, unsigned char *outlen,
        const unsigned char *in, unsigned int inlen, void *arg);
protected:
    virtual bool HandleInitial(std::shared_ptr<InitPacket> packet);
    virtual bool Handle0rtt(std::shared_ptr<Rtt0Packet> packet);
    virtual bool HandleHandshake(std::shared_ptr<HandShakePacket> packet);
    virtual bool HandleRetry(std::shared_ptr<RetryPacket> packet);
    virtual bool Handle1rtt(std::shared_ptr<Rtt1Packet> packet);

private:
    TLSServerConnection _tls_connection;
};

}

#endif