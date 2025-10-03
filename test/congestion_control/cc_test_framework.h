#ifndef TEST_CONGESTION_CONTROL_CC_TEST_FRAMEWORK
#define TEST_CONGESTION_CONTROL_CC_TEST_FRAMEWORK

#include <map>
#include <string>
#include <memory>
#include <vector>
#include <functional>

#include "network_simulator.h"
#include "quic/congestion_control/if_congestion_control.h"

namespace quicx {
namespace quic {
namespace test {

// Test metrics collected during simulation
struct CCTestMetrics {
    // Basic statistics
    uint64_t total_bytes_sent = 0;
    uint64_t total_bytes_acked = 0;
    uint64_t total_packets_sent = 0;
    uint64_t total_packets_lost = 0;
    double throughput_mbps = 0.0;
    double packet_loss_rate = 0.0;
    
    // Congestion window statistics
    double avg_cwnd_bytes = 0.0;
    double max_cwnd_bytes = 0.0;
    double min_cwnd_bytes = 0.0;
    
    // RTT statistics
    double avg_rtt_ms = 0.0;
    
    // Algorithm behavior
    uint64_t slow_start_duration_us = 0;
    uint64_t recovery_count = 0;
    double cwnd_growth_rate = 0.0;  // Average cwnd growth in slow start
    
    // State history for detailed analysis
    struct StateSnapshot {
        uint64_t time_us;
        uint64_t cwnd_bytes;
        uint64_t bytes_in_flight;
        uint64_t ssthresh_bytes;
        bool in_slow_start;
        bool in_recovery;
        uint64_t rtt_us;
    };
    std::vector<StateSnapshot> state_history;
};

// Test scenario configuration
struct TestScenario {
    std::string name;
    NetworkCondition network_condition;
    uint64_t test_duration_us;
    std::vector<std::pair<uint64_t, NetworkCondition>> condition_changes;  // Time -> new condition
    
    // Factory methods for common scenarios
    static TestScenario IdealNetwork(uint64_t duration_us = 5000000);
    static TestScenario LowLatencyNetwork(uint64_t duration_us = 10000000);
    static TestScenario HighLatencyNetwork(uint64_t duration_us = 10000000);
    static TestScenario MobileNetwork(uint64_t duration_us = 10000000);
    static TestScenario LossyNetwork(uint64_t duration_us = 10000000);
    static TestScenario BufferBloat(uint64_t duration_us = 10000000);
    static TestScenario SatelliteLink(uint64_t duration_us = 20000000);
    static TestScenario ExtremeLoss(uint64_t duration_us = 10000000);
    
    // Dynamic scenarios
    static TestScenario NetworkDegradation(uint64_t duration_us = 10000000);
    static TestScenario NetworkImprovement(uint64_t duration_us = 10000000);
    static TestScenario FluctuatingNetwork(uint64_t duration_us = 10000000);
};

// Unified test framework
class CCTestFramework {
public:
    // Factory function type for creating congestion control instances
    using CCFactory = std::function<std::unique_ptr<ICongestionControl>()>;
    
    CCTestFramework(CCFactory cc_factory, const TestScenario& scenario);
    
    // Run the test
    void Run();
    
    // Get test results
    const CCTestMetrics& GetMetrics() const { return metrics_; }
    
    // Print statistics
    void PrintStats(bool detailed = false) const;
    
private:
    void TrySendPackets();
    void SendPacket(uint64_t bytes);
    void ProcessAcks();
    void CheckForLostPackets();
    void RecordStateSnapshot();
    void CalculateMetrics();
    
    struct SentPacketInfo {
        uint64_t bytes;
        uint64_t sent_time;
    };
    
    std::unique_ptr<ICongestionControl> cc_;
    NetworkSimulator network_sim_;
    TestScenario scenario_;
    
    uint64_t current_time_us_;
    uint64_t next_packet_number_;
    uint64_t initial_cwnd_;
    uint64_t slow_start_exit_time_;
    uint64_t last_recovery_time_;
    
    std::map<uint64_t, SentPacketInfo> sent_packets_;
    CCTestMetrics metrics_;
};

// Algorithm factory helpers
class CCAlgorithmFactory {
public:
    static CCTestFramework::CCFactory Reno();
    static CCTestFramework::CCFactory CUBIC();
    static CCTestFramework::CCFactory BBRv1();
    static CCTestFramework::CCFactory BBRv2();
    static CCTestFramework::CCFactory BBRv3();
    
    static std::vector<std::pair<std::string, CCTestFramework::CCFactory>> AllAlgorithms();
};

} // namespace test
} // namespace quic
} // namespace quicx

#endif
