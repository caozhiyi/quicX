#include "quic/connection/controler/anti_amplification_controller.h"

#include "common/log/log.h"

namespace quicx {
namespace quic {

AntiAmplificationController::AntiAmplificationController()
    : is_unvalidated_(false),
      sent_bytes_(0),
      received_bytes_(0) {}

void AntiAmplificationController::EnterUnvalidatedState(uint64_t initial_credit) {
    is_unvalidated_ = true;
    sent_bytes_ = 0;
    received_bytes_ = initial_credit;

    common::LOG_DEBUG(
        "AntiAmplificationController::EnterUnvalidatedState: initial_credit=%llu, max_send=%llu",
        initial_credit, initial_credit * kAmplificationFactor);
}

void AntiAmplificationController::ExitUnvalidatedState() {
    if (!is_unvalidated_) {
        return;
    }

    common::LOG_DEBUG(
        "AntiAmplificationController::ExitUnvalidatedState: sent=%llu, received=%llu",
        sent_bytes_, received_bytes_);

    is_unvalidated_ = false;
    sent_bytes_ = 0;
    received_bytes_ = 0;
}

void AntiAmplificationController::OnBytesReceived(uint64_t bytes) {
    if (!is_unvalidated_) {
        return;
    }

    received_bytes_ += bytes;
    common::LOG_DEBUG(
        "AntiAmplificationController::OnBytesReceived: received %llu bytes, total=%llu, max_send=%llu",
        bytes, received_bytes_, received_bytes_ * kAmplificationFactor);
}

void AntiAmplificationController::OnBytesSent(uint64_t bytes) {
    if (!is_unvalidated_) {
        return;
    }

    sent_bytes_ += bytes;
    common::LOG_DEBUG(
        "AntiAmplificationController::OnBytesSent: sent %llu bytes, total=%llu, limit=%llu, remaining=%llu",
        bytes, sent_bytes_, received_bytes_ * kAmplificationFactor, GetRemainingBudget());
}

bool AntiAmplificationController::CanSend(uint64_t bytes) const {
    // If address is validated, no restrictions
    if (!is_unvalidated_) {
        return true;
    }

    // Check if sending would exceed the 3x amplification limit
    uint64_t max_allowed = received_bytes_ * kAmplificationFactor;
    bool can_send = (sent_bytes_ + bytes) <= max_allowed;

    if (!can_send) {
        common::LOG_DEBUG(
            "AntiAmplificationController::CanSend: BLOCKED - would exceed limit. "
            "sent=%llu, received=%llu, requested=%llu, limit=%llu",
            sent_bytes_, received_bytes_, bytes, max_allowed);
    }

    return can_send;
}

uint64_t AntiAmplificationController::GetRemainingBudget() const {
    // If validated, no budget restriction
    if (!is_unvalidated_) {
        return UINT64_MAX;
    }

    uint64_t max_allowed = received_bytes_ * kAmplificationFactor;

    // If already exceeded (shouldn't happen), return 0
    if (sent_bytes_ >= max_allowed) {
        return 0;
    }

    return max_allowed - sent_bytes_;
}

bool AntiAmplificationController::IsNearLimit() const {
    // If validated or no data received, not near limit
    if (!is_unvalidated_ || received_bytes_ == 0) {
        return false;
    }

    uint64_t max_allowed = received_bytes_ * kAmplificationFactor;
    uint64_t threshold = static_cast<uint64_t>(max_allowed * kNearLimitThreshold);

    bool near_limit = sent_bytes_ >= threshold;

    if (near_limit) {
        common::LOG_DEBUG(
            "AntiAmplificationController::IsNearLimit: approaching limit. "
            "sent=%llu, threshold=%llu (%.0f%%), limit=%llu",
            sent_bytes_, threshold, kNearLimitThreshold * 100, max_allowed);
    }

    return near_limit;
}

void AntiAmplificationController::Reset() {
    sent_bytes_ = 0;
    received_bytes_ = 0;

    common::LOG_DEBUG("AntiAmplificationController::Reset: counters reset (state=%s)",
        is_unvalidated_ ? "unvalidated" : "validated");
}

}  // namespace quic
}  // namespace quicx
