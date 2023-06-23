#ifndef QUIC_SERVER_SERVER
#define QUIC_SERVER_SERVER

#include <memory>
#include "quic/app/type.h"
#include "quic/app/app_interface.h"

namespace quicx {

class Server:
    public IApplication {
public:
    Server(RunThreadType run_type = RTT_SINGLE);
    virtual ~Server();

    bool Listen(const std::string& ip, uint16_t port);

    void MainLoop(uint16_t thread_num = 1);
};

}

#endif