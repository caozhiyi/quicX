#include "upgrade/network/if_tcp_socket.h"
#include "upgrade/handlers/http_smart_handler.h"

namespace quicx {
namespace upgrade {

HttpSmartHandler::HttpSmartHandler(const UpgradeSettings& settings, std::shared_ptr<common::IEventLoop> event_loop) 
    : BaseSmartHandler(settings, event_loop) {
}

bool HttpSmartHandler::InitializeConnection(std::shared_ptr<ITcpSocket> socket) {
    // HTTP connections don't need special initialization
    return true;
}

int HttpSmartHandler::ReadData(std::shared_ptr<ITcpSocket> socket, std::vector<uint8_t>& data) {
    return socket->Recv(data, 4096); // Read up to 4KB
}

int HttpSmartHandler::WriteData(std::shared_ptr<ITcpSocket> socket, std::vector<uint8_t>& data) {
    return socket->Send(data);
}

void HttpSmartHandler::CleanupConnection(std::shared_ptr<ITcpSocket> socket) {
    // HTTP connections don't need special cleanup
}

} // namespace upgrade
} // namespace quicx 