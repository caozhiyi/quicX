// #include "quic/quicx/receiver.h"
// #include "common/buffer/buffer.h"

// namespace quicx {
// namespace quic {

// Receiver::Receiver() {
//     _alloter = std::make_shared<common::BlockMemoryPool>(1500, 5);
// }

// bool Receiver::Listen(const std::string& ip, uint16_t port) {
//     return _udp_receiver.Listen(ip, port);
// }

// void Receiver::SetRecvSocket(uint64_t sock) {
//     _udp_receiver.SetRecvSocket(sock);
// }

// std::shared_ptr<UdpPacketIn> Receiver::DoRecv() {
//     std::shared_ptr<UdpPacketIn> udp_packet = std::make_shared<UdpPacketIn>();
//     auto buffer = std::make_shared<common::Buffer>(_alloter);
//     udp_packet->SetData(buffer);

//     auto ret = _udp_receiver.DoRecv(udp_packet);
//     if (ret == UdpReceiver::RR_SUCCESS) {
//         return udp_packet;
//     }
//     return nullptr;
// }

// }
// }
