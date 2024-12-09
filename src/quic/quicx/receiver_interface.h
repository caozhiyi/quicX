// #ifndef QUIC_QUICX_RECEIVER_INTERFACE
// #define QUIC_QUICX_RECEIVER_INTERFACE

// #include "quic/udp/udp_packet_in.h"

// namespace quicx {
// namespace quic {

// class IReceiver {
// public:
//     IReceiver() {}
//     virtual ~IReceiver() {}

//     virtual std::shared_ptr<UdpPacketIn> DoRecv() = 0;

//     virtual bool Listen(const std::string& ip, uint16_t port) = 0;
//     virtual void SetRecvSocket(uint64_t sock) = 0;

//     virtual void RegisteConnection(std::thread::id id, uint64_t cid_code) = 0;
//     virtual void CancelConnection(std::thread::id id, uint64_t cid_code) = 0;
// };

// }
// }

// #endif