// #ifndef QUIC_QUICX_RECEIVER
// #define QUIC_QUICX_RECEIVER

// #include <thread>
// #include "quic/udp/udp_receiver.h"
// #include "quic/udp/udp_packet_in.h"
// #include "common/alloter/pool_block.h"
// #include "quic/quicx/receiver_interface.h"

// namespace quicx {
// namespace quic {

// class Receiver:
//     public IReceiver {
// public:
//     Receiver();
//     virtual ~Receiver() {}

//     virtual bool Listen(const std::string& ip, uint16_t port);
//     virtual void SetRecvSocket(uint64_t sock);

//     virtual std::shared_ptr<UdpPacketIn> DoRecv();

//     void RegisteConnection(std::thread::id id, uint64_t cid_code) {}
//     void CancelConnection(std::thread::id id, uint64_t cid_code) {}

// protected:
//     UdpReceiver _udp_receiver;
//     std::shared_ptr<common::BlockMemoryPool> alloter_;
// };

// }
// }

// #endif