// #include "quic/quicx/receiver.h"
// #include "common/buffer/buffer.h"

// namespace quicx {
// namespace quic {

// Receiver::Receiver() {
//     alloter_ = std::make_shared<common::BlockMemoryPool>(1500, 5);
// }

// bool Receiver::Listen(const std::string& ip, uint16_t port) {
//     return udp_receiver_.Listen(ip, port);
// }

// void Receiver::SetRecvSocket(uint64_t sock) {
//     udp_receiver_.SetRecvSocket(sock);
// }

// std::shared_ptr<UdpPacketIn> Receiver::DoRecv() {
//     std::shared_ptr<UdpPacketIn> udp_packet = std::make_shared<UdpPacketIn>();
//     auto buffer = std::make_shared<common::Buffer>(alloter_);
//     udp_packet->SetData(buffer);

//     auto ret = udp_receiver_.DoRecv(udp_packet);
//     if (ret == UdpReceiver::RR_SUCCESS) {
//         return udp_packet;
//     }
//     return nullptr;
// }

// }
// }
