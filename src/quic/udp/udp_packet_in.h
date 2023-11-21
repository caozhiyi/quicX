#ifndef QUIC_UDP_PACKET_IN
#define QUIC_UDP_PACKET_IN

#include <vector>
#include "common/network/address.h"
#include "quic/packet/packet_interface.h"
#include "quic/udp/udp_packet_interface.h"

namespace quicx {
namespace quic {

class UdpPacketIn:
    public IUdpPacket  {
public:
    UdpPacketIn();
    ~UdpPacketIn();

    bool DecodePacket();

    void GetConnection(uint8_t* id, uint16_t& len);

    void SetPeerAddress(const common::Address&& addr) { _peer_addr = std::move(addr); }
    const common::Address& GetPeerAddress() const { return _peer_addr; }

    uint64_t GetConnectionHashCode() { return _connection_hash_code; }
    std::vector<std::shared_ptr<IPacket>>& GetPackets() { return _packets; }

    void SetRecvTime(uint64_t t) { _recv_time = t; }
    uint64_t GetRecvTime() { return _recv_time; }

private:
    uint64_t _recv_time;
    common::Address _peer_addr;
    uint8_t* _connection_id;
    uint16_t _connection_id_len;
    uint64_t _connection_hash_code;
    std::vector<std::shared_ptr<IPacket>> _packets;
};

}
}

#endif