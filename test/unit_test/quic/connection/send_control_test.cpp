#include <gtest/gtest.h>
#include <memory>
#include <tuple>
#include <vector>

#include "common/network/if_event_loop.h"
#include "common/timer/if_timer.h"
#include "common/timer/timer_task.h"

#include "quic/connection/controler/send_control.h"
#include "quic/frame/ack_frame.h"
#include "quic/frame/type.h"
#include "quic/packet/packet_number.h"
#include "quic/packet/rtt_1_packet.h"

namespace quicx {
namespace quic {
namespace {

class MockTimer: public common::ITimer {
public:
    uint32_t add_count = 0;
    uint32_t rm_count = 0;

    uint64_t AddTimer(common::TimerTask& task, uint32_t /*time*/, uint64_t /*now*/ = 0) override {
        add_count++;
        // Set task ID for test
        task.SetIdForTest(add_count);
        tasks_.push_back(task);
        return add_count;
    }

    bool RemoveTimer(common::TimerTask& task) override {
        // Find task by ID
        uint64_t id = task.GetId();
        for (auto it = tasks_.begin(); it != tasks_.end(); ++it) {
            if (it->GetId() == id) {
                tasks_.erase(it);
                rm_count++;
                return true;
            }
        }
        return false;
    }

    int32_t MinTime(uint64_t /*now*/ = 0) override { return tasks_.empty() ? -1 : 0; }
    void TimerRun(uint64_t /*now*/ = 0) override {}
    bool Empty() override { return tasks_.empty(); }

private:
    std::vector<common::TimerTask> tasks_;
};

std::shared_ptr<Rtt1Packet> MakePacket(uint64_t packet_number, FrameTypeBit frame_bits) {
    auto packet = std::make_shared<Rtt1Packet>();
    packet->SetPacketNumber(packet_number);
    packet->GetHeader()->SetPacketNumberLength(PacketNumber::GetPacketNumberLength(packet_number));
    packet->AddFrameTypeBit(frame_bits);
    return packet;
}

TEST(SendControlTest, AckElicitingPacketsTriggerCallbacks) {
    auto timer = std::make_shared<MockTimer>();
    auto event_loop = common::MakeEventLoop();
    ASSERT_TRUE(event_loop->Init());
    event_loop->SetTimerForTest(timer);
    SendControl send_control(timer);

    std::vector<std::tuple<uint64_t, uint64_t, bool>> callbacks;
    send_control.SetStreamDataAckCallback([&callbacks](uint64_t stream_id, uint64_t max_offset, bool has_fin) {
        callbacks.emplace_back(stream_id, max_offset, has_fin);
    });

    // Send two ack-eliciting packets carrying stream data
    auto pkt9 = MakePacket(9, FrameTypeBit::kStreamBit);
    std::vector<StreamDataInfo> data9 = {StreamDataInfo(4, 100, false)};
    send_control.OnPacketSend(0, pkt9, 1200, data9);

    auto pkt10 = MakePacket(10, FrameTypeBit::kStreamBit);
    std::vector<StreamDataInfo> data10 = {StreamDataInfo(4, 150, true)};
    send_control.OnPacketSend(0, pkt10, 1300, data10);

    // Expect 4 timer adds:
    // - 2 for packet timeout timers (one per packet)
    // - 2 for PTO timer (scheduled after each packet send, with the second one replacing the first)
    EXPECT_EQ(timer->add_count, 4u);

    auto ack = std::make_shared<AckFrame>();
    ack->SetLargestAck(10);
    ack->SetAckDelay(0);
    ack->SetFirstAckRange(1);  // Acknowledge packets 10 and 9

    send_control.OnPacketAck(10, PacketNumberSpace::kApplicationNumberSpace, ack);

    ASSERT_EQ(callbacks.size(), 2u);
    // Packet 10 (with FIN) is acknowledged first, followed by packet 9
    EXPECT_EQ(std::get<0>(callbacks[0]), 4u);
    EXPECT_EQ(std::get<1>(callbacks[0]), 150u);
    EXPECT_TRUE(std::get<2>(callbacks[0]));
    EXPECT_EQ(std::get<0>(callbacks[1]), 4u);
    EXPECT_EQ(std::get<1>(callbacks[1]), 100u);
    EXPECT_FALSE(std::get<2>(callbacks[1]));

    // Expect 4 timer removes:
    // - 1 for removing old PTO timer when sending packet 10
    // - 2 for packet timeout timers (cancelled when ACKed)
    // - 1 for PTO timer (cancelled when ACK received)
    EXPECT_EQ(timer->rm_count, 4u);
}

TEST(SendControlTest, NonAckElicitingPacketsAreNotTracked) {
    auto timer = std::make_shared<MockTimer>();
    auto event_loop = common::MakeEventLoop();
    ASSERT_TRUE(event_loop->Init());
    event_loop->SetTimerForTest(timer);
    SendControl send_control(timer);

    bool callback_invoked = false;
    send_control.SetStreamDataAckCallback([&callback_invoked](uint64_t /*stream_id*/, uint64_t /*max_offset*/,
                                              bool /*has_fin*/) { callback_invoked = true; });

    // Padding-only packet should not be considered ack-eliciting
    auto packet = MakePacket(1, FrameTypeBit::kPaddingBit);
    std::vector<StreamDataInfo> stream_info = {StreamDataInfo(8, 42, false)};
    send_control.OnPacketSend(0, packet, 1000, stream_info);

    EXPECT_EQ(timer->add_count, 0u);  // Timer not armed

    auto ack = std::make_shared<AckFrame>();
    ack->SetLargestAck(1);
    ack->SetAckDelay(0);
    ack->SetFirstAckRange(0);

    send_control.OnPacketAck(5, PacketNumberSpace::kApplicationNumberSpace, ack);
    EXPECT_FALSE(callback_invoked);
    EXPECT_EQ(timer->rm_count, 0u);
}

}  // namespace
}  // namespace quic
}  // namespace quicx
