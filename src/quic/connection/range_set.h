#ifndef QUIC_CONNECTION_RANGE_SET
#define QUIC_CONNECTION_RANGE_SET

#include <cstdint>
#include <iterator>
#include <map>
#include <utility>
#include <vector>

namespace quicx {
namespace quic {

// Disjoint, ascending closed ranges [lo, hi] over packet numbers.
//
// Replaces the per-packet-number std::set nodes that RecvControl used for ACK
// bookkeeping (report P1-P1): one map node per contiguous run instead of one
// per packet, and the runs are exactly what an ACK frame encodes.
//
// Invariants: ranges_ is ordered by lo, non-overlapping, and never adjacent
// (two ranges touching at a boundary are merged on insert). size_ tracks the
// element count (sum of hi-lo+1), not the range count.
class RangeSet {
public:
    // Insert a single value, merging with the neighbour(s) it touches.
    // Amortised O(log n) with no allocation when the value extends or falls
    // inside an existing range — the dominant case under contiguous arrival.
    void Insert(uint64_t v) {
        auto it = ranges_.lower_bound(v);  // first lo >= v
        if (it != ranges_.begin()) {
            auto prev = std::prev(it);
            if (prev->second >= v) {
                return;  // v already inside [prev.lo, prev.hi]
            }
            if (prev->second + 1 == v) {
                // Extend prev upward; may also touch the next range.
                if (it != ranges_.end() && it->first == v + 1) {
                    prev->second = it->second;
                    ranges_.erase(it);
                } else {
                    prev->second = v;
                }
                size_++;
                return;
            }
        }
        if (it != ranges_.end()) {
            if (it->first == v) {
                return;  // exact-lo duplicate
            }
            if (it->first == v + 1) {
                // Extend the next range downward.
                uint64_t hi = it->second;
                ranges_.erase(it);
                ranges_.emplace(v, hi);
                size_++;
                return;
            }
        }
        ranges_.emplace(v, v);
        size_++;
    }

    bool Empty() const { return ranges_.empty(); }

    // Number of tracked values (sum of range lengths), NOT range count.
    size_t Size() const { return size_; }

    size_t RangeCount() const { return ranges_.size(); }

    // Up to `max_ranges` largest ranges, in DESCENDING order, as (hi, lo)
    // pairs — the shape an ACK frame consumes (first range is the largest
    // acknowledged). Read-only: reporting does not consume (P0-C3).
    std::vector<std::pair<uint64_t, uint64_t>> LargestRanges(size_t max_ranges) const {
        std::vector<std::pair<uint64_t, uint64_t>> out;
        out.reserve(max_ranges);
        for (auto it = ranges_.rbegin(); it != ranges_.rend() && out.size() < max_ranges; ++it) {
            out.emplace_back(it->second, it->first);
        }
        return out;
    }

    // Drop every value <= bound (P0-C3: ack-of-ack pruning). Splits the range
    // straddling the bound if any.
    void EraseBelowOrEqual(uint64_t bound) {
        if (ranges_.empty()) {
            return;
        }
        auto it = ranges_.upper_bound(bound);  // first lo > bound
        if (it != ranges_.begin()) {
            auto prev = std::prev(it);
            if (prev->second > bound) {
                // Split: replace [prev.lo, prev.hi] with (bound+1, prev.hi]
                // (map keys are const, so re-seat the node).
                auto nh = ranges_.extract(prev);
                size_ -= bound - nh.key() + 1;
                nh.key() = bound + 1;
                ranges_.insert(std::move(nh));
                it = ranges_.upper_bound(bound);
            }
        }
        for (auto jt = ranges_.begin(); jt != it;) {
            size_ -= jt->second - jt->first + 1;
            jt = ranges_.erase(jt);
        }
    }

    void Clear() {
        ranges_.clear();
        size_ = 0;
    }

    // Drop every value in [lo, hi] (P0-C3: obligation fulfilled for exactly
    // the ranges an emitted ACK frame reported, including under
    // kMaxAckRanges truncation).
    void EraseRange(uint64_t lo, uint64_t hi) {
        if (hi < lo || ranges_.empty()) {
            return;
        }
        auto it = ranges_.lower_bound(lo);
        if (it != ranges_.begin() && std::prev(it)->second >= lo) {
            it = std::prev(it);  // predecessor straddles lo
        }
        while (it != ranges_.end() && it->first <= hi) {
            uint64_t rlo = it->first;
            uint64_t rhi = it->second;
            uint64_t del_lo = rlo > lo ? rlo : lo;
            uint64_t del_hi = rhi < hi ? rhi : hi;
            size_ -= del_hi - del_lo + 1;
            auto next = ranges_.erase(it);
            if (rlo < del_lo) {  // keep the head [rlo, del_lo-1]
                next = ranges_.emplace_hint(next, rlo, del_lo - 1);
                ++next;
            }
            if (rhi > del_hi) {  // keep the tail [del_hi+1, rhi]
                ranges_.emplace_hint(next, del_hi + 1, rhi);
                break;  // ranges are disjoint: nothing later intersects [lo, hi]
            }
            it = next;
        }
    }

private:
    std::map<uint64_t, uint64_t> ranges_;  // lo -> hi
    size_t size_ = 0;
};

}  // namespace quic
}  // namespace quicx

#endif  // QUIC_CONNECTION_RANGE_SET
