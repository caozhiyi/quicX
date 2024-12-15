// #include "common/log/log.h"
// #include "quic/quicx/receiver.h"
// #include "quic/quicx/processor.h"
// #include "quic/quicx/quicx_impl.h"
// #include "quic/quicx/thread_receiver.h"
// #include "quic/connection/client_connection.h"

// namespace quicx {
// namespace quic {

// QuicxImpl::QuicxImpl() {

// }

// QuicxImpl::~QuicxImpl() {

// }

// bool QuicxImpl::Init(uint16_t thread_num) {
//     ctx_ = std::make_shared<TLSCtx>();
//     if (!ctx_->Init()) {
//         common::LOG_ERROR("tls ctx init faliled.");
//         return false;
//     }

//     if (thread_num == 1) {
//         receiver_ = std::make_shared<Receiver>();

//     } else {
//         receiver_ = std::make_shared<ThreadReceiver>();
//     }

//     processors_.resize(thread_num);
//     for (size_t i = 0; i < thread_num; i++) {
//         auto processor = std::make_shared<Processor>();
//         processor->SetRecvFunction([this] { return receiver_->DoRecv(); });
//         processor->SetAddConnectionIDCB([this] (uint64_t id) { receiver_->RegisteConnection(std::this_thread::get_id(), id);});
//         processor->SetRetireConnectionIDCB([this] (uint64_t id) { receiver_->CancelConnection(std::this_thread::get_id(), id);});
//         processor->Start();
//         processors_.emplace_back(processor);
//     }
//     return true;
// }

// void QuicxImpl::Join() {
//     for (size_t i = 0; i < processors_.size(); i++) {
//         processors_[i]->Join();
//     }
    
// }

// void QuicxImpl::Destroy() {
//     if (receiver_) {
//         auto receiver = std::dynamic_pointer_cast<ThreadReceiver>(receiver_);
//         if (receiver) {
//             receiver->Stop();
//             receiver->WeakUp();
//         }
//     }

//     for (size_t i = 0; i < processors_.size(); i++) {
//         processors_[i]->Stop();
//         processors_[i]->WeakUp();
//     }
// }

// bool QuicxImpl::Connection(const std::string& ip, uint16_t port) {
//     if (!processors_.empty()) {
//         // TODO 
//         // 1. random select processor
//         // 2. client connectin manage
//         auto conn = processors_[0]->MakeClientConnection();
//         auto cli_conn = std::dynamic_pointer_cast<ClientConnection>(conn);
//         common::Address addr(common::AT_IPV4, ip, port);
//         if (cli_conn->Dial(addr)) {
//             receiver_->SetRecvSocket(cli_conn->GetSock());
//             return true;
//         }
//     }
//     return false;
// }

// bool QuicxImpl::ListenAndAccept(const std::string& ip, uint16_t port) {
//     if (receiver_) {
//         return receiver_->Listen(ip, port);
//     }
//     return false;
// }

// }
// }