#ifndef QUIC_CONNECTION_CONTROLER_RECV_MANAGER
#define QUIC_CONNECTION_CONTROLER_RECV_MANAGER

#include <memory>
#include "common/buffer/buffer.h"

namespace quicx {

// 管理 flow control/recv control
class RecvManager {
public:
    RecvManager();
    ~RecvManager();
};

}

#endif