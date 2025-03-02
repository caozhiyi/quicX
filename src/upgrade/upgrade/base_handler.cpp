#include "upgrade/upgrade/base_handler.h"

namespace quicx {
namespace upgrade {

void BaseHandler::SetTlsChecker(std::function<bool(common::Address&)> checker) {
    tls_checker_ = checker;
}

void BaseHandler::RegisterHandler(HandlerType type, std::shared_ptr<IfSocketHandler> handler) {
    handlers_[type] = handler;
}

void BaseHandler::HandleSocketConnect(std::shared_ptr<ISocket> socket) {
    if (tls_checker_) {
        auto addr = socket->GetRemoteAddress();
        if (!tls_checker_(addr)) {
            // TODO: tls handshake
        }
    }
}

void BaseHandler::HandleSocketData(std::shared_ptr<ISocket> socket, std::shared_ptr<common::IBufferRead> buffer) {
    // TODO, check proto type, dispatch to different handler
}

}
}