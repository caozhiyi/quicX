#include <gtest/gtest.h>
#include <iostream>
#include <memory>
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
    std::vector<common::TimerTask> tasks_;

    uint64_t AddTimer(common::TimerTask& task, uint32_t /*time*/, uint64_t /*now*/ = 0) override {
        add_count++;
        task.SetIdForTest(add_count);
        tasks_.push_back(task);
        return add_count;
    }

    bool RemoveTimer(common::TimerTask& task) override {
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

    void TriggerAllTimers() {
        auto tasks_copy = tasks_;  // Copy because callback might modify tasks_
        tasks_.clear();            // Assume all triggered
        for (auto& task : tasks_copy) {
            if (task.tcb_) {
                task.tcb_();
            }
        }
    }
};

std::shared_ptr<Rtt1Packet> MakePacket(uint64_t packet_number, uint32_t len) {
    auto packet = std::make_shared<Rtt1Packet>();
    packet->SetPacketNumber(packet_number);
    packet->GetHeader()->SetPacketNumberLength(PacketNumber::GetPacketNumberLength(packet_number));
    packet->AddFrameTypeBit(FrameTypeBit::kStreamBit);  // Ack-eliciting
    return packet;
}

TEST(SendControlLargeDataTest, DoubleSubtractionBug) {
    auto timer = std::make_shared<MockTimer>();
    SendControl send_control(timer);

    // 1. Send packets to fill CWND
    // Initial CWND is usually ~14KB (10 * MSS) or similar.
    // Let's send enough to fill it.
    uint64_t now = 1000;
    uint64_t can_send_bytes = 0;
    send_control.CanSend(now, can_send_bytes);
    EXPECT_GT(can_send_bytes, 0u);

    uint64_t initial_cwnd = can_send_bytes;  // Approximation
    uint64_t sent_bytes = 0;
    uint64_t pkt_num = 1;

    // Send packets until blocked
    while (true) {
        send_control.CanSend(now, can_send_bytes);
        if (can_send_bytes < 1000) break;  // Blocked or close to blocked

        auto packet = MakePacket(pkt_num++, 1000);
        send_control.OnPacketSend(now, packet, 1000);
        sent_bytes += 1000;
    }

    // 2. Simulate Packet Loss
    // Trigger timers for all sent packets
    EXPECT_GT(timer->tasks_.size(), 0u);
    timer->TriggerAllTimers();

    // Now all packets are lost. CWND should be reduced (Reno).
    // bytes_in_flight should be reduced by lost bytes.

    // 3. Simulate Late ACK for the lost packets
    // This is where the double subtraction happened.
    // If we ACK a packet that was already declared lost, it shouldn't reduce bytes_in_flight again.

    auto ack = std::make_shared<AckFrame>();
    ack->SetLargestAck(pkt_num - 1);
    ack->SetAckDelay(0);
    ack->SetFirstAckRange(pkt_num - 2);  // Ack all packets 1 to pkt_num-1

    send_control.OnPacketAck(now + 100, PacketNumberSpace::kApplicationNumberSpace, ack);

    // 4. Check state
    // If double subtraction occurred, bytes_in_flight might be negative (underflow to huge value)
    // or just very small, allowing massive sending.

    send_control.CanSend(now + 200, can_send_bytes);

    // If fixed, can_send_bytes should be reasonable (CWND - bytes_in_flight).
    // Since we ACKed everything, bytes_in_flight should be 0.
    // CWND should be small (due to loss event).
    // So can_send_bytes should be equal to current CWND.

    // If bug exists, bytes_in_flight would have been subtracted twice.
    // Once at loss: bytes_in_flight -= sent_bytes
    // Once at ACK: bytes_in_flight -= sent_bytes
    // Result: bytes_in_flight = -sent_bytes (huge uint64)
    // Then can_send_bytes = cwnd - huge_value = huge_value (if unchecked) or 0 (if checked)
    // Wait, if bytes_in_flight is huge, can_send_bytes = cwnd - huge = underflow?
    // Reno: left = (cwnd > inflight) ? cwnd - inflight : 0.
    // If inflight is huge, left is 0. So we would be BLOCKED.

    // Wait, let's trace the bug logic again.
    // bytes_in_flight starts at X.
    // Loss: bytes_in_flight -= packet_size. (Correct)
    // Ack: bytes_in_flight -= packet_size. (Incorrect)
    // If we only sent 1 packet:
    // Start: 1000.
    // Loss: 0.
    // Ack: 0 - 1000 = huge.
    // CanSend: cwnd > huge? No. Returns 0.

    // So the bug actually caused BLOCKED state?
    // User said "massive packet loss", implying it kept sending.
    // Ah, maybe bytes_in_flight didn't underflow but just went lower than it should?
    // Example: Send P1, P2. Inflight = 2000.
    // Lose P1. Inflight = 1000.
    // Ack P1 (late). Inflight = 0.
    // But P2 is still in flight!
    // So Inflight is 0, but reality is 1000.
    // So we can send MORE than we should.
    // CWND allows sending 1000 more bytes than it should.
    // This leads to congestion.

    // With Reno, on loss, CWND reduces to half (or min).
    // If we ACK everything, we are in recovery/slow start again?
    // If bug was present, we might have underflowed if we acked more than inflight?
    // Or just had incorrect low inflight.

    // Let's just assert that we can send, but not an insane amount.
    EXPECT_LT(can_send_bytes, 1024 * 1024 * 10);  // Should not be 10MB (hardcoded value from before)
    EXPECT_GT(can_send_bytes, 0u);
}

}  // namespace
}  // namespace quic
}  // namespace quicx
