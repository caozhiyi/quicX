#ifndef HTTP3_HTTP_CLIENT
#define HTTP3_HTTP_CLIENT

#include <memory>
#include "quic/include/if_quic.h"
#include "http3/include/if_client.h"
#include "quic/include/if_quic_connection.h"

namespace quicx {
namespace http3 {

class Client:
    public IClient {
public:
    Client();
    virtual ~Client();

    // Initialize the client with a certificate and a key
    virtual bool Init(const std::string& cert, const std::string& key,
                     uint16_t thread_num) override;

    // Start the client
    virtual bool Start() override;

    virtual bool Stop() override;

    // Send a request to the server
    virtual bool DoRequest(const IRequest& request, const http_handler handler) override;

private:
    std::shared_ptr<quic::IQuic> quic_;
    std::shared_ptr<quic::IQuicConnection> _conn;
};

}
}

#endif
