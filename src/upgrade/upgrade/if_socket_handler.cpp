#include "upgrade/upgrade/if_socket_handler.h"

namespace quicx {
namespace upgrade {

void ISocketHandler::RegisterHttpHandler(HttpVersion http_version, std::shared_ptr<IHttpHandler> handler) {
    http_handlers_[http_version] = handler;
}

}
}
