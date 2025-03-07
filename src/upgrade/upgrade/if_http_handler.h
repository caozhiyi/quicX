#ifndef UPGRADE_UPGRADE_IF_HTTP_HANDLER
#define UPGRADE_UPGRADE_IF_HTTP_HANDLER

#include <memory>

namespace quicx {
namespace upgrade {

/*
 * http handler interface
 * process http request and response
 */
class TcpSocket;
class IHttpHandler {
public:
    IHttpHandler() {}
    virtual ~IHttpHandler() {}

    virtual void HandleRequest(std::shared_ptr<TcpSocket> socket) = 0;
};

}
}


#endif