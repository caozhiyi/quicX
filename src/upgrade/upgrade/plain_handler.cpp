#include "common/log/log.h"
#include "common/network/io_handle.h"
#include "upgrade/network/tcp_socket.h"
#include "upgrade/upgrade/plain_handler.h"
#include "upgrade/network/if_tcp_action.h"

namespace quicx {
namespace upgrade {

void PlainHandler::HandleConnect(std::shared_ptr<TcpSocket> socket, std::shared_ptr<ITcpAction> action) {
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

        auto no_block_ret = common::SocketNoblocking(ret.return_value_);
        if (no_block_ret.return_value_ == -1) {
            common::LOG_ERROR("set socket no blocking failed. error:%d", no_block_ret.errno_);
            common::Close(ret.return_value_);
            continue;
        }

        auto new_socket = std::make_shared<TcpSocket>(ret.return_value_, addr, action, shared_from_this(), pool_block_);
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

    DispatchHttpHandler(socket);

    // process send data
    HandleWrite(socket);
}

void PlainHandler::HandleWrite(std::shared_ptr<TcpSocket> socket) {
    auto write_buffer = socket->GetWriteBuffer();
    if (!write_buffer) {
        common::LOG_ERROR("write buffer is nullptr");
        HandleClose(socket);
        return;
    }

    // if write buffer is empty, return
    if (write_buffer->GetDataLength() == 0) {
        return;
    }

    auto action = socket->GetAction();
    if (!action) {
        common::LOG_ERROR("action is nullptr");
        HandleClose(socket);
        return;
    }

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
                action->AddSender(socket);
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