#include "upgrade/upgrade/crypto_handler.h"

namespace quicx {
namespace upgrade {

void CryptoHandler::RegisterHandler(HttpVersion http_version, std::shared_ptr<IfSocketHandler> handler) {

}

void CryptoHandler::HandleSocketConnect(uint64_t listen_socket) {
    // plain transport, no need to do anything
}

void CryptoHandler::HandleSocketClose(std::shared_ptr<ISocket> socket) {
    // plain transport, no need to do anything
}

void CryptoHandler::ReadData(std::shared_ptr<ISocket> socket) {
    // plain transport, no need to do anything
}

void CryptoHandler::WriteData(std::shared_ptr<ISocket> socket) {
    // plain transport, no need to do anything
}
/*
connection: 
  1. plain 
  2. tls
reader writer:
  1. plain
  2. tls
http_handler:
  1. http/1.1
  2. http/2
*/
}
}