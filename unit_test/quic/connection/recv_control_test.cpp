#include <gtest/gtest.h>
#include <memory>

#include "quic/frame/type.h"
#include "quic/frame/ack_frame.h"
#include "common/timer/if_timer.h"
#include "common/timer/timer_task.h"
#include "quic/packet/rtt_1_packet.h"
#include "quic/packet/packet_number.h"
#include "quic/quicx/global_resource.h"
#include "quic/connection/controler/recv_control.h"

namespace quicx {
namespace quic {
namespace {

class MockTimer : public common::ITimer {
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

    bool RmTimer(common::TimerTask& task) override {
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

std::shared_ptr<Rtt1Packet> MakePacket(uint64_t number, FrameTypeBit frame_bits) {
    auto packet = std::make_shared<Rtt1Packet>();
    packet->SetPacketNumber(number);
    packet->GetHeader()->SetPacketNumberLength(PacketNumber::GetPacketNumberLength(number));
    packet->AddFrameTypeBit(frame_bits);
    return packet;
}

TEST(RecvControlTest, AckFrameGeneratedForAckElicitingPackets) {
    auto timer = std::make_shared<MockTimer>();
    GlobalResource::Instance().GetThreadLocalEventLoop()->SetTimerForTest(timer);
    RecvControl recv_control;

    recv_control.OnPacketRecv(100, MakePacket(5, FrameTypeBit::kStreamBit));
    recv_control.OnPacketRecv(101, MakePacket(4, FrameTypeBit::kStreamBit));
    recv_control.OnPacketRecv(150, MakePacket(2, FrameTypeBit::kStreamBit));

    EXPECT_EQ(timer->add_count, 1u);  // Timer armed only once despite multiple packets

    auto frame = recv_control.MayGenerateAckFrame(160, PacketNumberSpace::kApplicationNumberSpace, false);
    ASSERT_NE(frame, nullptr);
    auto ack = std::dynamic_pointer_cast<AckFrame>(frame);
    ASSERT_NE(ack, nullptr);

    EXPECT_EQ(ack->GetLargestAck(), 5u);
    EXPECT_EQ(ack->GetAckDelay(), 7u);  // (160 - 150) >> 3 with exponent default 3
    EXPECT_EQ(ack->GetFirstAckRange(), 1u);  // packets 5 and 4 contiguous

    const auto& ranges = ack->GetAckRange();
    ASSERT_EQ(ranges.size(), 1u);
    EXPECT_EQ(ranges[0].GetGap(), 1u);       // gap between 4 and 2 is one packet (packet 3 missing)
    EXPECT_EQ(ranges[0].GetAckRangeLength(), 0u);  // single packet range (packet 2)

    EXPECT_EQ(timer->rm_count, 1u);  // Timer cancelled when ACK generated
    GlobalResource::Instance().ResetForTest();
}

TEST(RecvControlTest, NonAckElicitingPacketsIgnored) {
    auto timer = std::make_shared<MockTimer>();
    GlobalResource::Instance().GetThreadLocalEventLoop()->SetTimerForTest(timer);
    RecvControl recv_control;

    recv_control.OnPacketRecv(200, MakePacket(10, FrameTypeBit::kAckBit));
    EXPECT_EQ(timer->add_count, 0u);

    auto frame = recv_control.MayGenerateAckFrame(205, PacketNumberSpace::kApplicationNumberSpace, false);
    EXPECT_EQ(frame, nullptr);
    EXPECT_EQ(timer->rm_count, 0u);
    GlobalResource::Instance().ResetForTest();
}

TEST(RecvControlTest, EcnCountersReportedInAckEcnFrame) {
    auto timer = std::make_shared<MockTimer>();
    GlobalResource::Instance().GetThreadLocalEventLoop()->SetTimerForTest(timer);
    RecvControl recv_control;

    // Two ack-eliciting packets so that ACK is generated later
    recv_control.OnPacketRecv(300, MakePacket(3, FrameTypeBit::kStreamBit));
    recv_control.OnPacketRecv(301, MakePacket(4, FrameTypeBit::kStreamBit));

    // Update ECN counters
    recv_control.OnEcnCounters(0x02, PacketNumberSpace::kApplicationNumberSpace);  // ECT(0)
    recv_control.OnEcnCounters(0x01, PacketNumberSpace::kApplicationNumberSpace);  // ECT(1)
    recv_control.OnEcnCounters(0x03, PacketNumberSpace::kApplicationNumberSpace);  // CE

    auto frame = recv_control.MayGenerateAckFrame(308, PacketNumberSpace::kApplicationNumberSpace, true);
    ASSERT_NE(frame, nullptr);

    auto ack_ecn = std::dynamic_pointer_cast<AckEcnFrame>(frame);
    ASSERT_NE(ack_ecn, nullptr);
    EXPECT_EQ(ack_ecn->GetEct0(), 1u);
    EXPECT_EQ(ack_ecn->GetEct1(), 1u);
    EXPECT_EQ(ack_ecn->GetEcnCe(), 1u);
    GlobalResource::Instance().ResetForTest();
}

}  // namespace
}  // namespace quic
}  // namespace quicx
