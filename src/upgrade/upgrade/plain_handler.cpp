#include "common/log/log.h"
#include "common/network/io_handle.h"
#include "upgrade/network/tcp_socket.h"
#include "upgrade/upgrade/plain_handler.h"
#include "upgrade/network/if_tcp_action.h"

namespace quicx {
namespace upgrade {

void PlainHandler::HandleConnect(std::shared_ptr<TcpSocket> socket, ITcpAction* action) {
    uint64_t sock = socket->GetSocket();

    // try to accept all connection
    while (true) {
        common::Address addr;
        auto ret = common::Accept(sock, addr);
        if (ret.return_value_ == -1) {
            if (ret.errno_ != 0) {
                common::LOG_ERROR("accept socket failed. error:%d", ret.errno_);
            }
            break;
        }

        auto new_socket = std::make_shared<TcpSocket>(ret.return_value_, addr, shared_from_this(), pool_block_);
        sockets_[ret.return_value_] = new_socket;
        common::LOG_DEBUG("accept a new tcp socket. socket: %d", ret.return_value_);
        action->AddReceiver(new_socket);
    }
}

void PlainHandler::HandleRead(std::shared_ptr<TcpSocket> socket) {
    auto read_buffer = socket->GetReadBuffer();

    // read data from socket
    while (true) {
        auto block = read_buffer->GetWriteBuffers(1024);
        auto ret = common::Recv(socket->GetSocket(), (char*)block->GetData(), block->GetFreeLength(), 0);
        if (ret.return_value_ > 0) {
            block->MoveWritePt(ret.return_value_);
            common::LOG_DEBUG("recv data from socket: %d, size: %d", socket->GetSocket(), ret.return_value_);

            // if read data is less than block size, break
            if (ret.return_value_ < block->GetFreeLength()) {
                break;
            }

        } else {
            if (ret.errno_ != 0) {
                common::LOG_ERROR("recv data failed. error:%d", ret.errno_);
                HandleClose(socket);
            }
            break;
        }
    }

    // check which http handler to use
    char data[14] = {0};
    read_buffer->ReadNotMovePt((uint8_t*)data, 14);

    // check if it's HTTP/2 by looking for the connection preface "PRI * HTTP/2.0"
    if (strncmp(data, "PRI * HTTP/2.0", 14) == 0) {
        auto iter = http_handlers_.find(HttpVersion::kHttp2);
        if (iter != http_handlers_.end()) {
            common::LOG_DEBUG("handle http2 request");
            iter->second->HandleRequest(socket);

        } else {
            common::LOG_ERROR("no http2 handler registered");
            HandleClose(socket);
        }
        return;
    }

    // check if it's HTTP/1.x by looking for common methods
    if (strncmp(data, "GET ", 4) == 0 || 
        strncmp(data, "POST ", 5) == 0 ||
        strncmp(data, "HEAD ", 5) == 0 ||
        strncmp(data, "PUT ", 4) == 0 ||
        strncmp(data, "DELETE ", 7) == 0 ||
        strncmp(data, "OPTIONS ", 8) == 0 ||
        strncmp(data, "TRACE ", 6) == 0 ||
        strncmp(data, "CONNECT ", 8) == 0) {
        auto iter = http_handlers_.find(HttpVersion::kHttp1);
        if (iter != http_handlers_.end()) {
            common::LOG_DEBUG("handle http1 request");
            iter->second->HandleRequest(socket);

        } else {
            common::LOG_ERROR("no http1 handler registered");
            HandleClose(socket);
        }
        return;
    }

    // unknown protocol
    common::LOG_ERROR("unknown protocol");
    HandleClose(socket);
}

void PlainHandler::HandleWrite(std::shared_ptr<TcpSocket> socket) {
    auto write_buffer = socket->GetWriteBuffer();

    // write data to socket
    while (write_buffer->GetDataLength() > 0) {
        auto block = write_buffer->GetReadBuffers();
        auto ret = common::Write(socket->GetSocket(), (char*)block->GetData(), block->GetDataLength());
        if (ret.return_value_ > 0) {
            block->MoveReadPt(ret.return_value_);
            common::LOG_DEBUG("send data to socket: %d, size: %d", socket->GetSocket(), ret.return_value_);

            // if write data is less than block size, break
            if (ret.return_value_ < block->GetDataLength()) {
                // need to write more data
                // action->AddSender(socket);
                break;
            }

        } else {
            if (ret.errno_ != 0) {
                common::LOG_ERROR("send data failed. error:%d", ret.errno_);
                HandleClose(socket);
            }
            break;
        }
    }
}

void PlainHandler::HandleClose(std::shared_ptr<TcpSocket> socket) {
    common::Close(socket->GetSocket());
    sockets_.erase(socket->GetSocket());
}

}
}