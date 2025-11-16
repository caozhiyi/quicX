#include <algorithm>
#include "network_simulator.h"

namespace quicx {
namespace quic {

NetworkSimulator::NetworkSimulator(const NetworkCondition& condition)
    : condition_(condition),
      rng_(condition.random_seed),
      loss_dist_(0.0, 1.0),
      queued_bytes_(0),
      last_dequeue_time_(0),
      next_event_index_(0) {
}

bool NetworkSimulator::SendPacket(uint64_t now, uint64_t packet_number, uint64_t bytes) {
    // Check if packet should be dropped due to loss
    if (condition_.packet_loss_rate > 0.0) {
        double random_val = loss_dist_(rng_);
        if (random_val < condition_.packet_loss_rate) {
            return false;  // Packet lost
        }
    }
    
    // Calculate delivery time
    uint64_t delivery_time = CalculateDeliveryTime(now, bytes);
    
    InFlightPacket packet;
    packet.packet_number = packet_number;
    packet.bytes = bytes;
    packet.sent_time = now;
    packet.delivery_time = delivery_time;
    
    in_flight_packets_.push(packet);
    queued_bytes_ += bytes;
    
    return true;  // Packet successfully sent
}

std::vector<InFlightPacket> NetworkSimulator::GetDeliveredPackets(uint64_t now) {
    std::vector<InFlightPacket> delivered;
    
    while (!in_flight_packets_.empty()) {
        const auto& packet = in_flight_packets_.front();
        if (packet.delivery_time <= now) {
            delivered.push_back(packet);
            queued_bytes_ -= packet.bytes;
            in_flight_packets_.pop();
        } else {
            break;  // Queue is sorted by time
        }
    }
    
    return delivered;
}

uint64_t NetworkSimulator::GetCurrentRtt(uint64_t now) const {
    uint64_t base_rtt = condition_.base_rtt_us;
    
    // If there's bandwidth limit and queue, calculate additional queuing delay
    if (condition_.bandwidth_bps > 0 && queued_bytes_ > 0) {
        // Queue delay = data in queue / bandwidth
        uint64_t queue_delay_us = (queued_bytes_ * 8 * 1000000) / condition_.bandwidth_bps;
        return base_rtt + queue_delay_us;
    }
    
    return base_rtt;
}

void NetworkSimulator::Reset() {
    while (!in_flight_packets_.empty()) {
        in_flight_packets_.pop();
    }
    queued_bytes_ = 0;
    last_dequeue_time_ = 0;
    scheduled_events_.clear();
    next_event_index_ = 0;
}

void NetworkSimulator::UpdateCondition(const NetworkCondition& condition) {
    condition_ = condition;
    rng_.seed(condition.random_seed);
}

void NetworkSimulator::ScheduleConditionChange(uint64_t time_us, const NetworkCondition& condition) {
    NetworkEvent event;
    event.time_us = time_us;
    event.condition = condition;
    scheduled_events_.push_back(event);
    
    // Sort events by time
    std::sort(scheduled_events_.begin(), scheduled_events_.end(),
              [](const NetworkEvent& a, const NetworkEvent& b) {
                  return a.time_us < b.time_us;
              });
    
    next_event_index_ = 0;
}

void NetworkSimulator::ApplyScheduledChanges(uint64_t now) {
    while (next_event_index_ < scheduled_events_.size()) {
        const auto& event = scheduled_events_[next_event_index_];
        if (event.time_us <= now) {
            condition_ = event.condition;
            next_event_index_++;
        } else {
            break;
        }
    }
}

uint64_t NetworkSimulator::CalculateDeliveryTime(uint64_t now, uint64_t bytes) {
    uint64_t rtt = GenerateRtt();
    uint64_t one_way_delay = rtt / 2;
    
    // If no bandwidth limit, return immediately with RTT delay
    if (condition_.bandwidth_bps == 0) {
        return now + one_way_delay;
    }
    
    // Calculate transmission time (serialization delay)
    uint64_t transmission_time_us = (bytes * 8 * 1000000) / condition_.bandwidth_bps;
    
    // Calculate queuing delay
    uint64_t queue_delay_us = 0;
    if (last_dequeue_time_ > now) {
        // If previous packet is still transmitting, need to wait
        queue_delay_us = last_dequeue_time_ - now;
    }
    
    // Check queue overflow - in reality, this would result in packet drop
    // For now we still accept the packet but this could be enhanced
    if (condition_.queue_size_bytes > 0) {
        uint64_t future_queue_size = queued_bytes_ + bytes;
        if (future_queue_size > condition_.queue_size_bytes) {
            // Queue is approaching full - calculate additional queuing delay
            // This simulates packets waiting in a deep queue
            uint64_t overflow = future_queue_size - condition_.queue_size_bytes;
            uint64_t overflow_delay = (overflow * 8 * 1000000) / condition_.bandwidth_bps;
            queue_delay_us += overflow_delay;
        }
    }
    
    uint64_t delivery_time = now + one_way_delay + transmission_time_us + queue_delay_us;
    last_dequeue_time_ = std::max(last_dequeue_time_, now) + transmission_time_us;
    
    return delivery_time;
}

uint64_t NetworkSimulator::GenerateRtt() {
    uint64_t rtt = condition_.base_rtt_us;
    
    if (condition_.rtt_jitter_us > 0) {
        // Generate random jitter in [-jitter, +jitter] range
        std::uniform_int_distribution<int64_t> jitter_dist(
            -static_cast<int64_t>(condition_.rtt_jitter_us),
            static_cast<int64_t>(condition_.rtt_jitter_us)
        );
        int64_t jitter = jitter_dist(rng_);
        rtt = static_cast<uint64_t>(std::max(static_cast<int64_t>(rtt) + jitter, static_cast<int64_t>(1000)));  // Minimum 1ms
    }
    
    return rtt;
}

} // namespace quic
} // namespace quicx