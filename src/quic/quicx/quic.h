#ifndef QUIC_QUICX_QUIC
#define QUIC_QUICX_QUIC

#include "quic/include/type.h"
#include "quic/quicx/if_master.h"

namespace quicx {
namespace quic {

class Quic {
public:
    Quic(const QuicTransportParams& params);
    virtual ~Quic();

    virtual void Join();

    virtual void Destroy();

    virtual void SetConnectionStateCallBack(connection_state_callback cb);

protected:
    void InitLogger(LogLevel level);

protected:
    QuicTransportParams params_;
    std::shared_ptr<IMaster> master_;
    connection_state_callback connection_state_cb_;
};

}
}

#endif