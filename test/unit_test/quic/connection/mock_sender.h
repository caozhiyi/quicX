#ifndef UTEST_CONNECTION_MOCK_SENDER
#define UTEST_CONNECTION_MOCK_SENDER

#include <memory>
#include <vector>
#include "quic/udp/if_sender.h"
#include "quic/udp/net_packet.h"
#include "common/buffer/if_buffer.h"

namespace quicx {
namespace quic {

/**
 * @brief Mock sender for unit tests
 *
 * Captures sent packets and provides them for inspection in tests.
 * Replaces the need for GenerateSendData() test helper method.
 */
class MockSender : public ISender {
public:
    MockSender() : socket_fd_(-1) {}
    virtual ~MockSender() {}

    virtual bool Send(std::shared_ptr<NetPacket>& pkt) override {
        if (!pkt) {
            return false;
        }
        sent_packets_.push_back(pkt);
        return true;
    }

    virtual int32_t GetSocket() const override {
        return socket_fd_;
    }

    // Test helper: Get all sent packets
    const std::vector<std::shared_ptr<NetPacket>>& GetSentPackets() const {
        return sent_packets_;
    }

    // Test helper: Get the last sent packet
    std::shared_ptr<NetPacket> GetLastSentPacket() const {
        if (sent_packets_.empty()) {
            return nullptr;
        }
        return sent_packets_.back();
    }

    // Test helper: Get last sent packet data as buffer
    std::shared_ptr<common::IBuffer> GetLastSentBuffer() const {
        auto pkt = GetLastSentPacket();
        if (!pkt) {
            return nullptr;
        }
        return pkt->GetData();
    }

    // Test helper: Clear captured packets
    void Clear() {
        sent_packets_.clear();
    }

    // Test helper: Check if any packet was sent
    bool HasSentPackets() const {
        return !sent_packets_.empty();
    }

    // Test helper: Get count of sent packets
    size_t GetSentPacketCount() const {
        return sent_packets_.size();
    }

private:
    int32_t socket_fd_;
    std::vector<std::shared_ptr<NetPacket>> sent_packets_;
};

}  // namespace quic
}  // namespace quicx

#endif  // UTEST_CONNECTION_MOCK_SENDER
