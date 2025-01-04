#ifndef QUIC_QUICX_QUIC
#define QUIC_QUICX_QUIC

#include <memory>
#include <vector>
#include "quic/include/if_quic.h"
#include "quic/quicx/if_processor.h"

namespace quicx {
namespace quic {

class Quic:
    public IQuic {
public:
    Quic();
    virtual ~Quic();

    virtual bool Init(uint16_t thread_num = 1);
    virtual bool Init(const std::string& cert_file, const std::string& key_file, uint16_t thread_num = 1);
    virtual bool Init(const char* cert_pem, const char* key_pem, uint16_t thread_num = 1);

    virtual void Join();

    virtual void Destroy();

    virtual bool Connection(const std::string& ip, uint16_t port, int32_t timeout_ms);

    virtual bool ListenAndAccept(const std::string& ip, uint16_t port);

    virtual void SetConnectionStateCallBack(connection_state_callback cb);

private:
    std::vector<std::shared_ptr<IProcessor>> processors_;
};

}
}

#endif