#ifndef QUIC_APP_CLIENT
#define QUIC_APP_CLIENT

#include <memory>
#include "quic/app/app_interface.h"
#include "quic/connection/client_connection.h"

namespace quicx {

class Client:
    public IApplication {
public:
    Client(RunThreadType run_type = RTT_SINGLE);
    virtual ~Client();

    std::shared_ptr<ClientConnection> Dail(const std::string& ip, uint16_t port);

    void MainLoop(uint16_t thread_num = 1);
};

}

#endif