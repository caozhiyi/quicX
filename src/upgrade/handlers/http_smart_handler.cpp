#include "upgrade/handlers/http_smart_handler.h"
#include "upgrade/network/if_tcp_socket.h"

namespace quicx {
namespace upgrade {

HttpSmartHandler::HttpSmartHandler(const UpgradeSettings& settings, std::shared_ptr<common::IEventLoop> event_loop):
    BaseSmartHandler(settings, event_loop) {}

bool HttpSmartHandler::InitializeConnection(std::shared_ptr<ITcpSocket> /*socket*/) {
    // HTTP connections don't need special initialization
    return true;
}

int HttpSmartHandler::ReadData(std::shared_ptr<ITcpSocket> socket, std::vector<uint8_t>& data) {
    int bytes = socket->Recv(data, 4096);  // Read up to 4KB
    if (bytes == 0) {
        // Unlike the TLS path there is no in-band handshake here, so a 0-byte
        // recv() is a real EOF (peer sent FIN). BaseSmartHandler would
        // otherwise read it as "no application bytes yet" and leave the fd
        // registered on a still-readable socket -- with a level-triggered
        // epoll that spins the loop at 100% CPU until the peer goes away.
        return -1;
    }
    return bytes;
}

int HttpSmartHandler::WriteData(std::shared_ptr<ITcpSocket> socket, std::vector<uint8_t>& data) {
    return socket->Send(data);
}

void HttpSmartHandler::CleanupConnection(std::shared_ptr<ITcpSocket> /*socket*/) {
    // HTTP connections don't need special cleanup
}

}  // namespace upgrade
}  // namespace quicx