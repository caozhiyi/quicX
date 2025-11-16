#include <algorithm>

#include "cc_test_framework.h"
#include "quic/congestion_control/reno_congestion_control.h"
#include "quic/congestion_control/cubic_congestion_control.h"
#include "quic/congestion_control/bbr_v1_congestion_control.h"
#include "quic/congestion_control/bbr_v2_congestion_control.h"
#include "quic/congestion_control/bbr_v3_congestion_control.h"



namespace quicx {
namespace quic {

// ========== TestScenario Factory Methods ==========

TestScenario TestScenario::IdealNetwork(uint64_t duration_us) {
    TestScenario scenario;
    scenario.name = "Ideal Network";
    scenario.network_condition = NetworkCondition::Ideal();
    scenario.test_duration_us = duration_us;
    return scenario;
}

TestScenario TestScenario::LowLatencyNetwork(uint64_t duration_us) {
    TestScenario scenario;
    scenario.name = "Low Latency Network";
    scenario.network_condition = NetworkCondition::LowLatency();
    scenario.test_duration_us = duration_us;
    return scenario;
}

TestScenario TestScenario::HighLatencyNetwork(uint64_t duration_us) {
    TestScenario scenario;
    scenario.name = "High Latency Network";
    scenario.network_condition = NetworkCondition::HighLatency();
    scenario.test_duration_us = duration_us;
    return scenario;
}

TestScenario TestScenario::MobileNetwork(uint64_t duration_us) {
    TestScenario scenario;
    scenario.name = "Mobile Network";
    scenario.network_condition = NetworkCondition::MobileNetwork();
    scenario.test_duration_us = duration_us;
    return scenario;
}

TestScenario TestScenario::LossyNetwork(uint64_t duration_us) {
    TestScenario scenario;
    scenario.name = "Lossy Network";
    scenario.network_condition = NetworkCondition::LossyNetwork();
    scenario.test_duration_us = duration_us;
    return scenario;
}

TestScenario TestScenario::BufferBloat(uint64_t duration_us) {
    TestScenario scenario;
    scenario.name = "Buffer Bloat";
    scenario.network_condition = NetworkCondition::BufferBloat();
    scenario.test_duration_us = duration_us;
    return scenario;
}

TestScenario TestScenario::SatelliteLink(uint64_t duration_us) {
    TestScenario scenario;
    scenario.name = "Satellite Link";
    NetworkCondition condition;
    condition.base_rtt_us = 600000;  // 600ms
    condition.rtt_jitter_us = 30000;
    condition.packet_loss_rate = 0.001;  // 0.1%
    condition.bandwidth_bps = 50 * 1000 * 1000 / 8;
    scenario.network_condition = condition;
    scenario.test_duration_us = duration_us;
    return scenario;
}

TestScenario TestScenario::ExtremeLoss(uint64_t duration_us) {
    TestScenario scenario;
    scenario.name = "Extreme Loss (10%)";
    NetworkCondition condition;
    condition.base_rtt_us = 50000;
    condition.packet_loss_rate = 0.10;  // 10%
    condition.bandwidth_bps = 10 * 1000 * 1000 / 8;
    scenario.network_condition = condition;
    scenario.test_duration_us = duration_us;
    return scenario;
}

TestScenario TestScenario::NetworkDegradation(uint64_t duration_us) {
    TestScenario scenario;
    scenario.name = "Network Degradation";
    scenario.network_condition = NetworkCondition::LowLatency();
    scenario.test_duration_us = duration_us;
    scenario.condition_changes.push_back({duration_us / 2, NetworkCondition::LossyNetwork()});
    return scenario;
}

TestScenario TestScenario::NetworkImprovement(uint64_t duration_us) {
    TestScenario scenario;
    scenario.name = "Network Improvement";
    scenario.network_condition = NetworkCondition::LossyNetwork();
    scenario.test_duration_us = duration_us;
    scenario.condition_changes.push_back({duration_us / 2, NetworkCondition::LowLatency()});
    return scenario;
}

TestScenario TestScenario::FluctuatingNetwork(uint64_t duration_us) {
    TestScenario scenario;
    scenario.name = "Fluctuating Network";
    scenario.network_condition = NetworkCondition::LowLatency();
    scenario.test_duration_us = duration_us;
    scenario.condition_changes.push_back({duration_us * 2 / 10, NetworkCondition::HighLatency()});
    scenario.condition_changes.push_back({duration_us * 4 / 10, NetworkCondition::LowLatency()});
    scenario.condition_changes.push_back({duration_us * 6 / 10, NetworkCondition::MobileNetwork()});
    scenario.condition_changes.push_back({duration_us * 8 / 10, NetworkCondition::LowLatency()});
    return scenario;
}

// ========== CCTestFramework Implementation ==========

CCTestFramework::CCTestFramework(CCFactory cc_factory, const TestScenario& scenario)
    : cc_(cc_factory()),
      network_sim_(scenario.network_condition),
      scenario_(scenario),
      current_time_us_(0),
      next_packet_number_(1),
      slow_start_exit_time_(0),
      last_recovery_time_(0) {
    
    // Configure congestion control
    CcConfigV2 cfg;
    cfg.mss_bytes = 1460;
    cfg.initial_cwnd_bytes = 10 * cfg.mss_bytes;
    cfg.min_cwnd_bytes = 2 * cfg.mss_bytes;
    cfg.max_cwnd_bytes = 1000 * cfg.mss_bytes;
    cc_->Configure(cfg);
    
    initial_cwnd_ = cfg.initial_cwnd_bytes;
    
    // Schedule network condition changes
    for (const auto& change : scenario.condition_changes) {
        network_sim_.ScheduleConditionChange(change.first, change.second);
    }
}

void CCTestFramework::Run() {
    while (current_time_us_ < scenario_.test_duration_us) {
        network_sim_.ApplyScheduledChanges(current_time_us_);
        
        // Process ACKs first to update cwnd
        ProcessAcks();
        
        // Then try to send packets based on updated cwnd
        TrySendPackets();
        
        // Record state every 10ms
        if (current_time_us_ % 10000 == 0) {
            RecordStateSnapshot();
        }
        
        current_time_us_ += 100;  // 0.1ms time step
    }
    
    CalculateMetrics();
}

void CCTestFramework::TrySendPackets() {
    // More realistic sending: send multiple packets in a burst up to cwnd limit
    // This simulates real network where application has data to send continuously
    const uint64_t packet_size = 1460;
    const uint64_t max_burst = 100;  // Reduced from 1000 to prevent test slowdown
    
    for (uint64_t i = 0; i < max_burst; i++) {
        uint64_t can_send_bytes = 0;
        auto send_state = cc_->CanSend(current_time_us_, can_send_bytes);
        
        if (send_state == ICongestionControl::SendState::kOk && can_send_bytes >= packet_size) {
            SendPacket(packet_size);
        } else {
            break;  // Can't send more (cwnd exhausted or blocked), exit
        }
    }
}

void CCTestFramework::SendPacket(uint64_t bytes) {
    uint64_t pn = next_packet_number_++;
    
    SentPacketEvent ev;
    ev.pn = pn;
    ev.bytes = bytes;
    ev.sent_time = current_time_us_;
    ev.is_retransmit = false;
    cc_->OnPacketSent(ev);
    
    bool sent = network_sim_.SendPacket(current_time_us_, pn, bytes);
    
    if (sent) {
        SentPacketInfo info;
        info.bytes = bytes;
        info.sent_time = current_time_us_;
        sent_packets_[pn] = info;
        
        metrics_.total_bytes_sent += bytes;
        metrics_.total_packets_sent++;
    } else {
        LossEvent loss_ev;
        loss_ev.pn = pn;
        loss_ev.bytes_lost = bytes;
        loss_ev.lost_time = current_time_us_;
        cc_->OnPacketLost(loss_ev);
        
        metrics_.total_packets_lost++;
    }
}

void CCTestFramework::ProcessAcks() {
    auto delivered = network_sim_.GetDeliveredPackets(current_time_us_);
    
    for (const auto& packet : delivered) {
        auto it = sent_packets_.find(packet.packet_number);
        if (it == sent_packets_.end()) {
            continue;
        }
        
        uint64_t rtt = current_time_us_ - it->second.sent_time;
        cc_->OnRoundTripSample(rtt, 0);
        
        AckEvent ack_ev;
        ack_ev.pn = packet.packet_number;
        ack_ev.bytes_acked = packet.bytes;
        ack_ev.ack_time = current_time_us_;
        ack_ev.ack_delay = 0;
        ack_ev.ecn_ce = false;
        cc_->OnPacketAcked(ack_ev);
        
        metrics_.total_bytes_acked += packet.bytes;
        sent_packets_.erase(it);
    }
    
    CheckForLostPackets();
}

void CCTestFramework::CheckForLostPackets() {
    uint64_t timeout_threshold = 3 * 100000;
    
    std::vector<uint64_t> lost_packets;
    for (const auto& pair : sent_packets_) {
        if (current_time_us_ - pair.second.sent_time > timeout_threshold) {
            lost_packets.push_back(pair.first);
        }
    }
    
    for (uint64_t pn : lost_packets) {
        auto it = sent_packets_.find(pn);
        if (it != sent_packets_.end()) {
            LossEvent loss_ev;
            loss_ev.pn = pn;
            loss_ev.bytes_lost = it->second.bytes;
            loss_ev.lost_time = current_time_us_;
            cc_->OnPacketLost(loss_ev);
            
            metrics_.total_packets_lost++;
            sent_packets_.erase(it);
        }
    }
}

void CCTestFramework::RecordStateSnapshot() {
    CCTestMetrics::StateSnapshot snapshot;
    snapshot.time_us = current_time_us_;
    snapshot.cwnd_bytes = cc_->GetCongestionWindow();
    snapshot.bytes_in_flight = cc_->GetBytesInFlight();
    snapshot.ssthresh_bytes = cc_->GetSsthresh();
    snapshot.in_slow_start = cc_->InSlowStart();
    snapshot.in_recovery = cc_->InRecovery();
    snapshot.rtt_us = network_sim_.GetCurrentRtt(current_time_us_);
    
    metrics_.state_history.push_back(snapshot);
    
    // Track slow start exit
    if (!snapshot.in_slow_start && slow_start_exit_time_ == 0) {
        slow_start_exit_time_ = current_time_us_;
    }
    
    // Track recovery periods
    if (snapshot.in_recovery) {
        if (last_recovery_time_ == 0 || (current_time_us_ - last_recovery_time_) > 100000) {
            metrics_.recovery_count++;
        }
        last_recovery_time_ = current_time_us_;
    }
}

void CCTestFramework::CalculateMetrics() {
    double duration_sec = scenario_.test_duration_us / 1000000.0;
    metrics_.throughput_mbps = (metrics_.total_bytes_acked * 8.0) / (duration_sec * 1000000.0);
    metrics_.packet_loss_rate = metrics_.total_packets_sent > 0 
        ? static_cast<double>(metrics_.total_packets_lost) / metrics_.total_packets_sent : 0.0;
    
    if (!metrics_.state_history.empty()) {
        double sum_cwnd = 0, sum_rtt = 0;
        metrics_.max_cwnd_bytes = 0;
        metrics_.min_cwnd_bytes = std::numeric_limits<double>::max();
        
        for (const auto& snapshot : metrics_.state_history) {
            sum_cwnd += snapshot.cwnd_bytes;
            sum_rtt += snapshot.rtt_us;
            metrics_.max_cwnd_bytes = std::max(metrics_.max_cwnd_bytes, static_cast<double>(snapshot.cwnd_bytes));
            metrics_.min_cwnd_bytes = std::min(metrics_.min_cwnd_bytes, static_cast<double>(snapshot.cwnd_bytes));
        }
        
        metrics_.avg_cwnd_bytes = sum_cwnd / metrics_.state_history.size();
        metrics_.avg_rtt_ms = (sum_rtt / metrics_.state_history.size()) / 1000.0;
    }
    
    metrics_.slow_start_duration_us = slow_start_exit_time_ > 0 ? slow_start_exit_time_ : scenario_.test_duration_us;
    
    // Calculate cwnd growth rate in slow start
    if (metrics_.state_history.size() >= 2) {
        size_t ss_samples = 0;
        double cwnd_growth = 0;
        
        for (size_t i = 1; i < metrics_.state_history.size(); ++i) {
            if (metrics_.state_history[i-1].in_slow_start && 
                metrics_.state_history[i].in_slow_start) {
                int64_t growth = static_cast<int64_t>(metrics_.state_history[i].cwnd_bytes) - 
                                static_cast<int64_t>(metrics_.state_history[i-1].cwnd_bytes);
                cwnd_growth += growth;
                ss_samples++;
            }
        }
        
        metrics_.cwnd_growth_rate = ss_samples > 0 ? cwnd_growth / ss_samples : 0.0;
    }
}

void CCTestFramework::PrintStats(bool detailed) const {
    printf("\n=== %s Test Results ===\n", scenario_.name.c_str());
    printf("Duration: %.2f seconds\n", scenario_.test_duration_us / 1000000.0);
    printf("Throughput: %.2f Mbps\n", metrics_.throughput_mbps);
    printf("Bytes Sent: %lu, Acked: %lu\n", metrics_.total_bytes_sent, metrics_.total_bytes_acked);
    printf("Loss Rate: %.2f%%\n", metrics_.packet_loss_rate * 100);
    printf("Avg RTT: %.2f ms\n", metrics_.avg_rtt_ms);
    
    if (detailed) {
        printf("\n--- Detailed Metrics ---\n");
        printf("Avg Cwnd: %.2f KB\n", metrics_.avg_cwnd_bytes / 1024.0);
        printf("Max Cwnd: %.2f KB\n", metrics_.max_cwnd_bytes / 1024.0);
        printf("Min Cwnd: %.2f KB\n", metrics_.min_cwnd_bytes / 1024.0);
        printf("Slow Start Duration: %.2f s\n", metrics_.slow_start_duration_us / 1000000.0);
        printf("Recovery Count: %lu\n", metrics_.recovery_count);
        printf("Cwnd Growth Rate: %.2f bytes/RTT\n", metrics_.cwnd_growth_rate);
    }
    
    printf("========================\n\n");
}

// ========== CCAlgorithmFactory Implementation ==========

CCTestFramework::CCFactory CCAlgorithmFactory::Reno() {
    return []() { return std::make_unique<RenoCongestionControl>(); };
}

CCTestFramework::CCFactory CCAlgorithmFactory::CUBIC() {
    return []() { return std::make_unique<CubicCongestionControl>(); };
}

CCTestFramework::CCFactory CCAlgorithmFactory::BBRv1() {
    return []() { return std::make_unique<BBRv1CongestionControl>(); };
}

CCTestFramework::CCFactory CCAlgorithmFactory::BBRv2() {
    return []() { return std::make_unique<BBRv2CongestionControl>(); };
}

CCTestFramework::CCFactory CCAlgorithmFactory::BBRv3() {
    return []() { return std::make_unique<BBRv3CongestionControl>(); };
}

std::vector<std::pair<std::string, CCTestFramework::CCFactory>> CCAlgorithmFactory::AllAlgorithms() {
    return {
        {"Reno", Reno()},
        {"CUBIC", CUBIC()},
        {"BBRv1", BBRv1()},
        {"BBRv2", BBRv2()},
        {"BBRv3", BBRv3()}
    };
}

} // namespace quic
} // namespace quicx
