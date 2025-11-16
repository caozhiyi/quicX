#ifndef TEST_CONGESTION_CONTROL_NETWORK_SIMULATOR
#define TEST_CONGESTION_CONTROL_NETWORK_SIMULATOR

#include <cstdint>
#include <random>
#include <queue>
#include <vector>

namespace quicx {
namespace quic {

// Network environment configuration
struct NetworkCondition {
    // Base RTT in microseconds
    uint64_t base_rtt_us = 50000;  // Default 50ms
    
    // RTT jitter range in microseconds, actual RTT will be in [base_rtt - jitter, base_rtt + jitter]
    uint64_t rtt_jitter_us = 5000;  // Default ±5ms
    
    // Packet loss rate (0.0 - 1.0)
    double packet_loss_rate = 0.0;
    
    // Bandwidth limit in bytes/second, 0 means unlimited
    uint64_t bandwidth_bps = 0;
    
    // Queue size in bytes for simulating buffer bloat, 0 means unlimited
    uint64_t queue_size_bytes = 0;
    
    // Random seed for reproducible tests
    uint32_t random_seed = 42;

    // Preset network environments
    static NetworkCondition Ideal() {
        NetworkCondition cond;
        cond.base_rtt_us = 10000;  // 10ms
        cond.rtt_jitter_us = 0;
        cond.packet_loss_rate = 0.0;
        cond.bandwidth_bps = 0;  // Unlimited
        return cond;
    }

    static NetworkCondition LowLatency() {
        NetworkCondition cond;
        cond.base_rtt_us = 20000;  // 20ms
        cond.rtt_jitter_us = 2000;  // ±2ms
        cond.packet_loss_rate = 0.001;  // 0.1%
        cond.bandwidth_bps = 100 * 1000 * 1000 / 8;  // 100Mbps
        return cond;
    }

    static NetworkCondition HighLatency() {
        NetworkCondition cond;
        cond.base_rtt_us = 200000;  // 200ms
        cond.rtt_jitter_us = 20000;  // ±20ms
        cond.packet_loss_rate = 0.01;  // 1%
        cond.bandwidth_bps = 10 * 1000 * 1000 / 8;  // 10Mbps
        return cond;
    }

    static NetworkCondition MobileNetwork() {
        NetworkCondition cond;
        cond.base_rtt_us = 100000;  // 100ms
        cond.rtt_jitter_us = 30000;  // ±30ms
        cond.packet_loss_rate = 0.02;  // 2%
        cond.bandwidth_bps = 5 * 1000 * 1000 / 8;  // 5Mbps
        cond.queue_size_bytes = 100 * 1460;  // Buffer for 100 packets
        return cond;
    }

    static NetworkCondition LossyNetwork() {
        NetworkCondition cond;
        cond.base_rtt_us = 50000;  // 50ms
        cond.rtt_jitter_us = 10000;  // ±10ms
        cond.packet_loss_rate = 0.05;  // 5%
        cond.bandwidth_bps = 20 * 1000 * 1000 / 8;  // 20Mbps
        return cond;
    }

    static NetworkCondition BufferBloat() {
        NetworkCondition cond;
        cond.base_rtt_us = 30000;  // 30ms base RTT
        cond.rtt_jitter_us = 5000;
        cond.packet_loss_rate = 0.001;
        cond.bandwidth_bps = 10 * 1000 * 1000 / 8;  // 10Mbps
        cond.queue_size_bytes = 500 * 1460;  // Large buffer
        return cond;
    }

    // Additional scenarios for comprehensive testing
    static NetworkCondition Satellite() {
        NetworkCondition cond;
        cond.base_rtt_us = 600000;  // 600ms (geostationary satellite)
        cond.rtt_jitter_us = 50000;  // ±50ms
        cond.packet_loss_rate = 0.005;  // 0.5%
        cond.bandwidth_bps = 50 * 1000 * 1000 / 8;  // 50Mbps
        return cond;
    }

    static NetworkCondition Unstable() {
        NetworkCondition cond;
        cond.base_rtt_us = 80000;  // 80ms
        cond.rtt_jitter_us = 40000;  // ±40ms (high variance)
        cond.packet_loss_rate = 0.03;  // 3%
        cond.bandwidth_bps = 8 * 1000 * 1000 / 8;  // 8Mbps
        cond.queue_size_bytes = 150 * 1460;
        return cond;
    }
    
    // Realistic enterprise network (typical office/datacenter)
    static NetworkCondition EnterpriseNetwork() {
        NetworkCondition cond;
        cond.base_rtt_us = 5000;  // 5ms (low latency)
        cond.rtt_jitter_us = 500;  // ±0.5ms
        cond.packet_loss_rate = 0.0001;  // 0.01% (very low loss)
        cond.bandwidth_bps = 1000 * 1000 * 1000 / 8;  // 1Gbps
        cond.queue_size_bytes = 1000 * 1460;  // Large queue (support high BDP)
        return cond;
    }
    
    // Typical broadband connection (home internet)
    static NetworkCondition Broadband() {
        NetworkCondition cond;
        cond.base_rtt_us = 15000;  // 15ms
        cond.rtt_jitter_us = 3000;  // ±3ms
        cond.packet_loss_rate = 0.002;  // 0.2%
        cond.bandwidth_bps = 100 * 1000 * 1000 / 8;  // 100Mbps
        cond.queue_size_bytes = 500 * 1460;  // Moderate buffer
        return cond;
    }
    
    // 5G mobile network
    static NetworkCondition FiveG() {
        NetworkCondition cond;
        cond.base_rtt_us = 20000;  // 20ms
        cond.rtt_jitter_us = 5000;  // ±5ms
        cond.packet_loss_rate = 0.005;  // 0.5%
        cond.bandwidth_bps = 200 * 1000 * 1000 / 8;  // 200Mbps
        cond.queue_size_bytes = 800 * 1460;
        return cond;
    }
    
    // LTE/4G mobile network
    static NetworkCondition LTE() {
        NetworkCondition cond;
        cond.base_rtt_us = 50000;  // 50ms
        cond.rtt_jitter_us = 15000;  // ±15ms
        cond.packet_loss_rate = 0.01;  // 1%
        cond.bandwidth_bps = 50 * 1000 * 1000 / 8;  // 50Mbps
        cond.queue_size_bytes = 400 * 1460;
        return cond;
    }
    
    // WiFi network (typical home/office WiFi)
    static NetworkCondition WiFi() {
        NetworkCondition cond;
        cond.base_rtt_us = 8000;  // 8ms
        cond.rtt_jitter_us = 4000;  // ±4ms (can vary)
        cond.packet_loss_rate = 0.003;  // 0.3%
        cond.bandwidth_bps = 300 * 1000 * 1000 / 8;  // 300Mbps (WiFi 5/6)
        cond.queue_size_bytes = 600 * 1460;
        return cond;
    }
    
    // Cross-continent network (e.g., US-Europe)
    static NetworkCondition CrossContinent() {
        NetworkCondition cond;
        cond.base_rtt_us = 150000;  // 150ms
        cond.rtt_jitter_us = 10000;  // ±10ms
        cond.packet_loss_rate = 0.003;  // 0.3%
        cond.bandwidth_bps = 100 * 1000 * 1000 / 8;  // 100Mbps
        cond.queue_size_bytes = 1500 * 1460;  // Large buffer for high BDP
        return cond;
    }
};

// In-flight packet
struct InFlightPacket {
    uint64_t packet_number;
    uint64_t bytes;
    uint64_t sent_time;
    uint64_t delivery_time;  // Expected delivery time
};

// Network event for dynamic condition changes
struct NetworkEvent {
    uint64_t time_us;  // When to apply this condition
    NetworkCondition condition;
};

// Network simulator
class NetworkSimulator {
public:
    explicit NetworkSimulator(const NetworkCondition& condition);
    
    // Send a packet, returns whether it was dropped
    bool SendPacket(uint64_t now, uint64_t packet_number, uint64_t bytes);
    
    // Get packets delivered at specified time
    std::vector<InFlightPacket> GetDeliveredPackets(uint64_t now);
    
    // Calculate current actual RTT (including queuing delay)
    uint64_t GetCurrentRtt(uint64_t now) const;
    
    // Get bytes in queue
    uint64_t GetQueuedBytes() const { return queued_bytes_; }
    
    // Reset simulator
    void Reset();
    
    // Update network conditions (for simulating dynamic network changes)
    void UpdateCondition(const NetworkCondition& condition);
    
    // Schedule network condition changes for dynamic testing
    void ScheduleConditionChange(uint64_t time_us, const NetworkCondition& condition);
    
    // Apply scheduled condition changes if any
    void ApplyScheduledChanges(uint64_t now);
    
    // Get current network condition
    const NetworkCondition& GetCurrentCondition() const { return condition_; }

private:
    NetworkCondition condition_;
    std::mt19937 rng_;
    std::uniform_real_distribution<double> loss_dist_;
    
    // Transmission queue
    std::queue<InFlightPacket> in_flight_packets_;
    uint64_t queued_bytes_;
    uint64_t last_dequeue_time_;
    
    // Scheduled network events
    std::vector<NetworkEvent> scheduled_events_;
    size_t next_event_index_;
    
    // Calculate packet transmission time and queuing delay
    uint64_t CalculateDeliveryTime(uint64_t now, uint64_t bytes);
    
    // Generate RTT with jitter
    uint64_t GenerateRtt();
};

} // namespace quic
} // namespace quicx

#endif