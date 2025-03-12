#include "common/log/log.h"
#include "upgrade/network/tcp_socket.h"
#include "upgrade/http1/http1_handler.h"
#include "upgrade/http2/http2_handler.h"
#include "upgrade/upgrade/if_socket_handler.h"

namespace quicx {
namespace upgrade {

ISocketHandler::ISocketHandler() {
    pool_block_ = std::make_shared<common::BlockMemoryPool>(1024, 20);
}

void ISocketHandler::DispatchHttpHandler(std::shared_ptr<TcpSocket> socket) {
    // check which http handler to use
    char data[14] = {0};
    auto read_buffer = socket->GetReadBuffer();
    read_buffer->ReadNotMovePt((uint8_t*)data, 14);

    // check if it's HTTP/1.x by looking for common methods
    if (strncmp(data, "GET ", 4) == 0 || 
        strncmp(data, "POST ", 5) == 0 ||
        strncmp(data, "HEAD ", 5) == 0 ||
        strncmp(data, "PUT ", 4) == 0 ||
        strncmp(data, "DELETE ", 7) == 0 ||
        strncmp(data, "OPTIONS ", 8) == 0 ||
        strncmp(data, "TRACE ", 6) == 0 ||
        strncmp(data, "CONNECT ", 8) == 0) {
        http_1_handler_->HandleRequest(socket);
        return;
    }

    // default to http/2
    http_2_handler_->HandleRequest(socket);
}

void ISocketHandler::BuildHttpHandler() {
    http_1_handler_ = std::make_unique<Http1Handler>();
    http_2_handler_ = std::make_unique<Http2Handler>();
}

}
}
