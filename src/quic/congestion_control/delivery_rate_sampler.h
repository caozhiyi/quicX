#ifndef QUIC_CONGESTION_CONTROL_DELIVERY_RATE_SAMPLER
#define QUIC_CONGESTION_CONTROL_DELIVERY_RATE_SAMPLER

#include <cstdint>
#include <map>

namespace quicx {
namespace quic {

/**
 * @brief BBR delivery-rate sampler (P1-CC1)
 *
 * BBR draft §4.5.2 / Cardwell et al. 2016 §3.2. Every ACK becomes a rate
 * sample by snapshotting the cumulative delivered bytes/time at send time
 * and dividing the delivered delta by the elapsed interval:
 *
 *   send_elapsed = P.sent_time - P.first_sent_time   (span of the window
 *                                                    the packet was sent into)
 *   ack_elapsed  = now - P.delivered_time            (span since the last
 *                                                    delivery the sender saw)
 *   interval     = max(send_elapsed, ack_elapsed)
 *   rate         = (delivered_now - delivered_then) / interval
 *
 * Taking the MAX of the two elapsed spans (not their sum, and not
 * ack_time - first_sent_time, which adds a full RTT on top of the send
 * span) keeps the sample unbiased in both window-limited steady state and
 * ACK-clocked bursts. This replaces the former SRTT-window byte aggregation
 * in BBRv1, which produced one coarse sample per RTT and could not
 * distinguish app-limited stretches from real ones.
 */
class DeliveryRateSampler {
public:
    struct RateSample {
        uint64_t delivery_rate_bps = 0;  // bytes/sec
        // True when the sample was taken inside an application-limited
        // window: the rate reflects the app's supply, not the path. Callers
        // should discard it unless it exceeds their current max estimate.
        bool is_app_limited = false;
        bool valid = false;
    };

    void OnPacketSent(uint64_t pn, uint64_t bytes, uint64_t sent_time_us) {
        // first_sent_time: the send time of the oldest packet still in
        // flight — the window the rate sample will span. When nothing is in
        // flight the sample degenerates to this packet alone.
        uint64_t first_sent = inflight_.empty() ? sent_time_us : inflight_.begin()->second.sent_time_us;
        inflight_[pn] = PacketInfo{bytes, sent_time_us, first_sent, delivered_, delivered_time_us_,
            app_limited_};
    }

    void OnPacketAcked(uint64_t pn, uint64_t bytes_acked, uint64_t ack_time_us, RateSample& out) {
        out = RateSample{};
        auto it = inflight_.find(pn);
        if (it == inflight_.end()) {
            return;  // duplicate/late ACK (or a packet we never snapshotted)
        }
        const PacketInfo p = it->second;
        inflight_.erase(it);

        // send_elapsed: how long the packet's send window already spanned.
        uint64_t send_elapsed = (p.sent_time_us > p.first_sent_time_us) ? p.sent_time_us - p.first_sent_time_us : 0;
        // ack_elapsed: how long since the last delivery the sender observed
        // when this packet was sent (0 = none yet, so the interval spans
        // the whole pre-send history — the BBR draft definition).
        uint64_t ack_elapsed = (ack_time_us > p.delivered_time_us) ? ack_time_us - p.delivered_time_us : 0;
        uint64_t interval_us = send_elapsed > ack_elapsed ? send_elapsed : ack_elapsed;

        delivered_ += bytes_acked;
        delivered_time_us_ = ack_time_us;

        if (interval_us > 0) {
            uint64_t delivered_delta = delivered_ - p.delivered;
            // bytes/us → bytes/sec, saturating.
            uint64_t rate = 0;
            if (delivered_delta <= UINT64_MAX / 1000000ull) {
                rate = delivered_delta * 1000000ull / interval_us;
            } else {
                rate = UINT64_MAX;
            }
            out.delivery_rate_bps = rate;
            out.is_app_limited = p.is_app_limited;
            out.valid = true;
        }
    }

    void OnPacketLost(uint64_t pn) {
        inflight_.erase(pn);  // lost bytes never entered delivered_
    }

    // Mark that the sender is currently application-limited (window not
    // filled by the app): applies to snapshots taken from now on.
    void SetAppLimited() { app_limited_ = true; }
    void ClearAppLimited() { app_limited_ = false; }
    bool IsAppLimited() const { return app_limited_; }

    uint64_t delivered_bytes() const { return delivered_; }
    size_t tracked_packets() const { return inflight_.size(); }

    void Reset() {
        inflight_.clear();
        delivered_ = 0;
        delivered_time_us_ = 0;
        app_limited_ = false;
    }

private:
    struct PacketInfo {
        uint64_t bytes;
        uint64_t sent_time_us;
        uint64_t first_sent_time_us;  // oldest in-flight send time at snapshot
        uint64_t delivered;           // cumulative delivered bytes at snapshot
        uint64_t delivered_time_us;   // time of the last delivery at snapshot
        bool is_app_limited;
    };

    // Ordered by PN: begin() is the oldest in-flight snapshot.
    std::map<uint64_t, PacketInfo> inflight_;
    uint64_t delivered_ = 0;
    uint64_t delivered_time_us_ = 0;  // time of the most recent delivery
    bool app_limited_ = false;
};

}  // namespace quic
}  // namespace quicx

#endif  // QUIC_CONGESTION_CONTROL_DELIVERY_RATE_SAMPLER
