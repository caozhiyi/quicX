#ifndef QUIC_CONTROLLER_CONTROLLER
#define QUIC_CONTROLLER_CONTROLLER

#include <vector>
#include <memory>
#include <string>
#include <cstdint>
#include <unordered_map>

namespace quicx {

class IPacket;
class IBufferRead;
class UdpListener;
class IConnection;
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
    bool GetDestConnectionId(const std::vector<std::shared_ptr<IPacket>>& packets, uint8_t* &cid, uint16_t& len);

private:
    std::shared_ptr<UdpListener> _listener;
    std::unordered_map<uint64_t, std::shared_ptr<IConnection>> _conn_map;
};

}

#endif