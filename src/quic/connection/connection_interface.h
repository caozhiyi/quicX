#ifndef QUIC_CONNECTION_CONNECTION_INTERFACE
#define QUIC_CONNECTION_CONNECTION_INTERFACE

#include <memory>
#include <vector>
#include <cstdint>
#include <unordered_set>
#include "quic/crypto/tls/type.h"
#include "quic/packet/init_packet.h"
#include "quic/packet/retry_packet.h"
#include "quic/packet/rtt_0_packet.h"
#include "quic/packet/rtt_1_packet.h"
#include "quic/packet/hand_shake_packet.h"
#include "quic/packet/packet_interface.h"
#include "quic/connection/transport_param.h"
#include "quic/crypto/cryptographer_interface.h"
#include "quic/crypto/tls/tls_server_conneciton.h"

namespace quicx {

class IConnection:
    public TlsHandlerInterface {
public:
    IConnection();
    virtual ~IConnection();

    void AddConnectionId(uint8_t* id, uint16_t len);
    void RetireConnectionId(uint8_t* id, uint16_t len);
    // TODO 
    // 1. 为流配置允许的最小初始数量
    // 2. 设置流级别及连接级别的流量限制, 限制接收缓存的的大小
    // 3. 识别握手成功或进行中
    // 4. 保持连接不被关闭, 发送PING等帧
    // 5. 立即关闭连接
    virtual void Close() = 0;

    // Encryption correlation function
    virtual void SetReadSecret(SSL* ssl, EncryptionLevel level, const SSL_CIPHER *cipher,
        const uint8_t *secret, size_t secret_len);

    virtual void SetWriteSecret(SSL* ssl, EncryptionLevel level, const SSL_CIPHER *cipher,
        const uint8_t *secret, size_t secret_len);

    virtual void WriteMessage(EncryptionLevel level, const uint8_t *data,
        size_t len);

    virtual void FlushFlight();
    virtual void SendAlert(EncryptionLevel level, uint8_t alert);

    virtual void HandlePacket(std::vector<std::shared_ptr<IPacket>>& packets);

protected:
    virtual bool HandleInitial(std::shared_ptr<InitPacket> packet) = 0;
    virtual bool Handle0rtt(std::shared_ptr<Rtt0Packet> packet) = 0;
    virtual bool HandleHandshake(std::shared_ptr<HandShakePacket> packet) = 0;
    virtual bool HandleRetry(std::shared_ptr<RetryPacket> packet) = 0;
    virtual bool Handle1rtt(std::shared_ptr<Rtt1Packet> packet) = 0;

protected:
    TransportParam _transport_param;
    std::unordered_set<std::string> _conn_id_set;
    std::shared_ptr<ICryptographer> _cryptographers[NUM_ENCRYPTION_LEVELS]; 
};

}

#endif