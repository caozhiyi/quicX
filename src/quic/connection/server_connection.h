#ifndef QUIC_CONNECTION_SERVER_CONNECTION
#define QUIC_CONNECTION_SERVER_CONNECTION

#include <memory>
#include <cstdint>
#include "quic/connection/base_connection.h"
#include "quic/crypto/tls/tls_server_conneciton.h"

namespace quicx {

class ServerConnection:
    public BaseConnection,
    public TlsServerHandlerInterface {
public:
    ServerConnection(std::shared_ptr<TLSCtx> ctx);
    virtual ~ServerConnection();
    // TODO
    // 1. 监听传入的连接
    // 2. 如果支持早期数据，在发送给客户端的TLS恢复ticket中嵌入应用层控制数据
    // 3. 如果支持早期数据，从接收自客户端的恢复ticket中恢复应用层控制数据，并根据该信息接受或拒绝早期数据。
    virtual void Close();

    bool GenerateSendData(std::shared_ptr<IBuffer> buffer);
    void SSLAlpnSelect(const unsigned char **out, unsigned char *outlen,
        const unsigned char *in, unsigned int inlen, void *arg);

protected:
    virtual bool OnInitialPacket(std::shared_ptr<IPacket> packet);
    virtual bool On0rttPacket(std::shared_ptr<IPacket> packet);
    virtual bool OnHandshakePacket(std::shared_ptr<IPacket> packet);
    virtual bool OnRetryPacket(std::shared_ptr<IPacket> packet);
    virtual bool On1rttPacket(std::shared_ptr<IPacket> packet);

private:
    std::shared_ptr<TLSServerConnection> _tls_connection;
};

}

#endif