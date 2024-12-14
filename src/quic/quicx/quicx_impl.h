// #ifndef QUIC_QUICX_QUICX_IMPL
// #define QUIC_QUICX_QUICX_IMPL

// #include <memory>
// #include <vector>
// #include "quic/include/quicx.h"
// #include "common/thread/thread.h"
// #include "quic/crypto/tls/tls_ctx.h"
// #include "quic/quicx/receiver_interface.h"
// #include "quic/quicx/processor_interface.h"

// namespace quicx {
// namespace quic {

// class QuicxImpl:
//     public Quicx {
// public:
//     QuicxImpl();
//     virtual ~QuicxImpl();

//     virtual bool Init(uint16_t thread_num);
//     virtual void Join();
//     virtual void Destroy();
//     virtual bool Connection(const std::string& ip, uint16_t port);
//     virtual bool ListenAndAccept(const std::string& ip, uint16_t port);
// private:
//     std::shared_ptr<TLSCtx> ctx_;
//     std::shared_ptr<IReceiver> receiver_;
//     std::vector<std::shared_ptr<IProcessor>> _processors;
// };

// }
// }

// #endif