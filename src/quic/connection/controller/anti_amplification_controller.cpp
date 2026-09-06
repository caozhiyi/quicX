#include "common/log/log.h"

#include "quic/connection/controller/anti_amplification_controller.h"

namespace quicx {
namespace quic {

AntiAmplificationController::AntiAmplificationController():
    is_unvalidated_(false),
    sent_bytes_(0),
    received_bytes_(0) {}

void AntiAmplificationController::EnterUnvalidatedState(uint64_t initial_credit) {
    received_bytes_.store(initial_credit, std::memory_order_release);
    sent_bytes_.store(0, std::memory_order_release);
    is_unvalidated_.store(true, std::memory_order_release);

    LOG_DEBUG("AntiAmplificationController::EnterUnvalidatedState: initial_credit=%llu, max_send=%llu", initial_credit,
        initial_credit * kAmplificationFactor);
}

void AntiAmplificationController::ExitUnvalidatedState() {
    if (!is_unvalidated_.load(std::memory_order_acquire)) {
        return;
    }

    LOG_DEBUG("AntiAmplificationController::ExitUnvalidatedState: sent=%llu, received=%llu",
        sent_bytes_.load(std::memory_order_relaxed), received_bytes_.load(std::memory_order_relaxed));

    // Flip the flag first (release) so concurrent TryCharge() callers stop gating;
    // counters are just bookkeeping once validated.
    is_unvalidated_.store(false, std::memory_order_release);
    sent_bytes_.store(0, std::memory_order_relaxed);
    received_bytes_.store(0, std::memory_order_relaxed);
}

void AntiAmplificationController::OnBytesReceived(uint64_t bytes) {
    if (!is_unvalidated_.load(std::memory_order_acquire)) {
        return;
    }

    uint64_t total = received_bytes_.fetch_add(bytes, std::memory_order_relaxed) + bytes;
    LOG_DEBUG("AntiAmplificationController::OnBytesReceived: received %llu bytes, total=%llu, max_send=%llu", bytes,
        total, total * kAmplificationFactor);
}

void AntiAmplificationController::OnBytesSent(uint64_t bytes) {
    if (!is_unvalidated_.load(std::memory_order_acquire)) {
        return;
    }

    uint64_t total = sent_bytes_.fetch_add(bytes, std::memory_order_relaxed) + bytes;
    LOG_DEBUG("AntiAmplificationController::OnBytesSent: sent %llu bytes, total=%llu, limit=%llu, remaining=%llu",
        bytes, total, received_bytes_.load(std::memory_order_relaxed) * kAmplificationFactor, GetRemainingBudget());
}

bool AntiAmplificationController::TryCharge(uint64_t bytes) {
    // Already validated: unrestricted, no accounting needed.
    if (!is_unvalidated_.load(std::memory_order_acquire)) {
        return true;
    }

    // Atomic check-and-debit: a CAS loop on sent_bytes_ so two concurrent senders
    // can't both pass the budget check and then over-debit past the 3x budget.
    // received_bytes_ is read relaxed; a slightly stale (smaller) value only
    // tightens the limit, which keeps us within RFC 9000 Section 8.1.
    uint64_t cur = sent_bytes_.load(std::memory_order_relaxed);
    while (true) {
        const uint64_t limit = received_bytes_.load(std::memory_order_relaxed) * kAmplificationFactor;
        if (bytes > limit || cur > limit - bytes) {
            return false;  // would exceed the 3x amplification budget
        }
        if (sent_bytes_.compare_exchange_weak(cur, cur + bytes, std::memory_order_acq_rel, std::memory_order_relaxed)) {
            return true;
        }
        // cur refreshed by CAS failure; retry with the latest value.
    }
}

bool AntiAmplificationController::CanSend(uint64_t bytes) const {
    // If address is validated, no restrictions
    if (!is_unvalidated_.load(std::memory_order_acquire)) {
        return true;
    }

    // Check if sending would exceed the 3x amplification limit
    uint64_t received = received_bytes_.load(std::memory_order_relaxed);
    uint64_t sent = sent_bytes_.load(std::memory_order_relaxed);
    uint64_t max_allowed = received * kAmplificationFactor;
    bool can_send = (sent + bytes) <= max_allowed;

    if (!can_send) {
        LOG_DEBUG(
            "AntiAmplificationController::CanSend: BLOCKED - would exceed limit. "
            "sent=%llu, received=%llu, requested=%llu, limit=%llu",
            sent, received, bytes, max_allowed);
    }

    return can_send;
}

uint64_t AntiAmplificationController::GetRemainingBudget() const {
    // If validated, no budget restriction
    if (!is_unvalidated_.load(std::memory_order_acquire)) {
        return UINT64_MAX;
    }

    uint64_t received = received_bytes_.load(std::memory_order_relaxed);
    uint64_t sent = sent_bytes_.load(std::memory_order_relaxed);
    uint64_t max_allowed = received * kAmplificationFactor;

    // If already exceeded (shouldn't happen), return 0
    if (sent >= max_allowed) {
        return 0;
    }

    return max_allowed - sent;
}

bool AntiAmplificationController::IsNearLimit() const {
    // If validated or no data received, not near limit
    if (!is_unvalidated_.load(std::memory_order_acquire) || received_bytes_.load(std::memory_order_relaxed) == 0) {
        return false;
    }

    uint64_t received = received_bytes_.load(std::memory_order_relaxed);
    uint64_t sent = sent_bytes_.load(std::memory_order_relaxed);
    uint64_t max_allowed = received * kAmplificationFactor;
    uint64_t threshold = static_cast<uint64_t>(max_allowed * kNearLimitThreshold);

    bool near_limit = sent >= threshold;

    if (near_limit) {
        LOG_DEBUG(
            "AntiAmplificationController::IsNearLimit: approaching limit. "
            "sent=%llu, threshold=%llu (%.0f%%), limit=%llu",
            sent, threshold, kNearLimitThreshold * 100, max_allowed);
    }

    return near_limit;
}

void AntiAmplificationController::Reset() {
    sent_bytes_.store(0, std::memory_order_relaxed);
    received_bytes_.store(0, std::memory_order_relaxed);

    LOG_DEBUG("AntiAmplificationController::Reset: counters reset (state=%s)",
        is_unvalidated_.load(std::memory_order_acquire) ? "unvalidated" : "validated");
}

}  // namespace quic
}  // namespace quicx
