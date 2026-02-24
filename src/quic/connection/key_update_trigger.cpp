#include "common/log/log.h"
#include "quic/connection/key_update_trigger.h"

namespace quicx {
namespace quic {

// Default threshold: 512KB of data before triggering key update
static const uint64_t kDefaultBytesThreshold = 512 * 1024;

// Default packet number threshold: 1000 packets
static const uint64_t kDefaultPacketNumberThreshold = 1000;

KeyUpdateTrigger::KeyUpdateTrigger():
    enabled_(false),
    triggered_(false),
    key_update_count_(0),
    bytes_threshold_(kDefaultBytesThreshold),
    total_bytes_sent_(0),
    pn_threshold_(kDefaultPacketNumberThreshold),
    last_pn_at_update_(0),
    current_pn_(0) {
}

bool KeyUpdateTrigger::OnBytesSent(uint64_t bytes_sent) {
    if (!enabled_) {
        return false;
    }
    
    total_bytes_sent_ += bytes_sent;
    
    if (!triggered_ && total_bytes_sent_ >= bytes_threshold_) {
        common::LOG_INFO("Key update triggered: bytes sent (%llu) >= threshold (%llu)",
            total_bytes_sent_, bytes_threshold_);
        return true;
    }
    
    return false;
}

bool KeyUpdateTrigger::OnPacketSent(uint64_t packet_number) {
    if (!enabled_) {
        return false;
    }
    
    current_pn_ = packet_number;
    
    if (!triggered_ && pn_threshold_ > 0) {
        uint64_t packets_since_update = current_pn_ - last_pn_at_update_;
        if (packets_since_update >= pn_threshold_) {
            common::LOG_INFO("Key update triggered: packets since update (%llu) >= threshold (%llu)",
                packets_since_update, pn_threshold_);
            return true;
        }
    }
    
    return false;
}

bool KeyUpdateTrigger::ShouldTriggerKeyUpdate() const {
    if (!enabled_ || triggered_) {
        return false;
    }
    
    // Check bytes threshold
    if (bytes_threshold_ > 0 && total_bytes_sent_ >= bytes_threshold_) {
        return true;
    }
    
    // Check packet number threshold
    if (pn_threshold_ > 0) {
        uint64_t packets_since_update = current_pn_ - last_pn_at_update_;
        if (packets_since_update >= pn_threshold_) {
            return true;
        }
    }
    
    return false;
}

void KeyUpdateTrigger::MarkTriggered() {
    triggered_ = true;
    key_update_count_++;
    last_pn_at_update_ = current_pn_;
    common::LOG_INFO("Key update #%u completed at packet number %llu", key_update_count_, current_pn_);
}

void KeyUpdateTrigger::Reset() {
    triggered_ = false;
    total_bytes_sent_ = 0;
    // Note: we don't reset key_update_count_ as it tracks total updates
    // last_pn_at_update_ keeps its value for next threshold calculation
}

}  // namespace quic
}  // namespace quicx
