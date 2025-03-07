#include "upgrade/upgrade/crypto_handler.h"

namespace quicx {
namespace upgrade {

void CryptoHandler::HandleConnect(std::shared_ptr<ISocket> socket, ITcpAction* action) {
    // plain transport, no need to do anything
}

void CryptoHandler::HandleClose(std::shared_ptr<ISocket> socket) {
    // plain transport, no need to do anything
}

void CryptoHandler::HandleRead(std::shared_ptr<ISocket> socket) {
    // plain transport, no need to do anything
}

void CryptoHandler::HandleWrite(std::shared_ptr<ISocket> socket) {
    // plain transport, no need to do anything
}

void CryptoHandler::ReadData(std::shared_ptr<ISocket> socket) {
    // plain transport, no need to do anything
}

void CryptoHandler::WriteData(std::shared_ptr<ISocket> socket) {
    // plain transport, no need to do anything
}

}
}