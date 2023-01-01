#ifndef QUIC_CONTROLLER_CONTROLLER
#define QUIC_CONTROLLER_CONTROLLER

#include <memory>
#include <string>
#include <cstdint>

namespace quicx {

class IPacket;
class IBufferRead;
class UdpListener;
class Controller {
public:
    Controller();
    ~Controller();

    /*
     * initialize encryption library.
     * ciphers: cipher suite
     * prefer_server_ciphers: preferred server encryption suite
     * cert_path: certificate path
     * key_path: key file address
     * key_pwd: key file password
     */
    bool SetCrypto(const std::string& ciphers, bool prefer_server_ciphers, const std::string& cert_path, const std::string& key_path, const std::string& key_pwd);

    bool Listen(const std::string& ip, uint16_t port);

    bool Stop();

private:
    void Dispatcher(std::shared_ptr<IBufferRead> recv_data);
    bool HandleInitial(std::shared_ptr<IPacket> packet);
    bool Handle0rtt(std::shared_ptr<IPacket> packet);
    bool HandleHandshake(std::shared_ptr<IPacket> packet);
    bool HandleRetry(std::shared_ptr<IPacket> packet);
    bool HandleNegotiation(std::shared_ptr<IPacket> packet);
    bool Handle1rtt(std::shared_ptr<IPacket> packet);

private:
    std::shared_ptr<UdpListener> _listener;
};

}

#endif