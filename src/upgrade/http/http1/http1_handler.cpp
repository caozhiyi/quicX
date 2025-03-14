#include "upgrade/network/tcp_socket.h"
#include "upgrade/http/http1/http1_handler.h"

namespace quicx {
namespace upgrade {

void Http1Handler::HandleRequest(std::shared_ptr<TcpSocket> socket) {
    // Read all request data from socket buffer
    // check if the request is legal
    auto read_buffer = socket->GetReadBuffer();
    
    // Send Alt-Svc response header to upgrade to HTTP/3
    auto write_buffer = socket->GetWriteBuffer();
    std::string response = "HTTP/1.1 200 OK\r\n"
                          "Alt-Svc: h3=\":443\"\r\n"
                          "Content-Length: 0\r\n"
                          "\r\n";
    
    write_buffer->Write((uint8_t*)response.c_str(), response.length());
}

bool Http1Handler::ParseRequest(std::shared_ptr<common::IBufferChains> buffer) {
    return true;
}

}
}
