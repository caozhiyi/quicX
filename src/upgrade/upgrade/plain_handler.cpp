#include "upgrade/upgrade/plain_handler.h"

namespace quicx {
namespace upgrade {

void PlainHandler::RegisterHandler(HttpVersion http_version, std::shared_ptr<ISocketHandler> handler) {

}

void PlainHandler::HandleSocketConnect(uint64_t listen_socket) {
    // plain transport, no need to do anything
}

void PlainHandler::HandleSocketClose(std::shared_ptr<ISocket> socket) {
    // plain transport, no need to do anything
}

void PlainHandler::ReadData(std::shared_ptr<ISocket> socket) {
    // plain transport, no need to do anything
}

void PlainHandler::WriteData(std::shared_ptr<ISocket> socket) {
    // plain transport, no need to do anything
}

}
}