#include "upgrade/upgrade/if_socket_handler.h"

namespace quicx {
namespace upgrade {

ISocketHandler::ISocketHandler() {
    pool_block_ = std::make_shared<common::BlockMemoryPool>(1024, 20);
}

void ISocketHandler::RegisterHttpHandler(HttpVersion http_version, std::shared_ptr<IHttpHandler> handler) {
    http_handlers_[http_version] = handler;
}

}
}
