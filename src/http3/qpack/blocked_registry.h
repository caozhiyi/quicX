#ifndef HTTP3_QPACK_BLOCKED_REGISTRY
#define HTTP3_QPACK_BLOCKED_REGISTRY

#include <cstdint>
#include <functional>
#include <set>
#include <unordered_map>

namespace quicx {
namespace http3 {

class QpackBlockedRegistry {
public:
    QpackBlockedRegistry();
    ~QpackBlockedRegistry();
    
    // Set maximum number of blocked streams (from SETTINGS_QPACK_BLOCKED_STREAMS).
    //
    // RFC 9204 §5: SETTINGS_QPACK_BLOCKED_STREAMS specifies the upper bound
    // on the number of streams that may be blocked.  A value of 0 means the
    // peer's encoder MUST NOT cause any stream to become blocked — i.e. it
    // MUST always emit header blocks with Required Insert Count = 0.
    // Therefore, here `max_blocked` == 0 means "no blocking allowed" (every
    // Add() will fail).  To express "unlimited" (e.g. for tests/profiling),
    // pass UINT64_MAX explicitly.
    void SetMaxBlockedStreams(uint64_t max_blocked) {
        max_blocked_streams_ = max_blocked;
        max_blocked_explicit_ = true;
    }
    
    // Get current number of blocked streams
    uint64_t GetBlockedCount() const { return pending_.size(); }
    
    // Check if we can add another blocked stream
    bool CanAddBlocked() const {
        // If SetMaxBlockedStreams was never called, default to unlimited
        // (preserves prior behaviour for code paths that construct a
        // registry without configuring SETTINGS).
        if (!max_blocked_explicit_) {
            return true;
        }
        return pending_.size() < max_blocked_streams_;
    }
    
    // Enqueue a blocked header block by key (e.g., stream_id) with a retry closure
    // Returns false if max blocked streams limit reached
    bool Add(uint64_t key, const std::function<void()>& retry_fn);
    
    // Ack a section (by key), immediate retry and erase
    void Ack(uint64_t key);
    void Remove(uint64_t key);
    // Insert count increment — try to resume all
    void NotifyAll();

    // RFC 9204 §4.4.1 / §4.4.2: Section Ack and Stream Cancellation carry only
    // the Stream ID on the wire. When a receiver parses such frames it only
    // knows the stream id and must match the earliest outstanding header
    // section on that stream (keys are stored as (stream_id<<32)|section_no,
    // so the earliest one has the smallest value among keys sharing the same
    // high 32 bits).
    //
    // AckByStreamId: finds the earliest pending section for stream_id,
    //                invokes its retry callback, and erases it.
    // RemoveByStreamId: finds the earliest pending section for stream_id and
    //                   erases it WITHOUT invoking the retry callback.
    // Returns true when an entry was found and processed.
    bool AckByStreamId(uint64_t stream_id);
    bool RemoveByStreamId(uint64_t stream_id);

private:
    // SETTINGS_QPACK_BLOCKED_STREAMS configured limit.  Only consulted when
    // max_blocked_explicit_ is true; otherwise the registry is unlimited.
    uint64_t max_blocked_streams_{0};
    bool max_blocked_explicit_{false};
    std::unordered_map<uint64_t, std::function<void()>> pending_;
    // Secondary index: stream_id -> ordered set of section keys outstanding
    // for that stream.  Lets AckByStreamId / RemoveByStreamId locate the
    // earliest section (smallest key) in O(log K) where K is the number of
    // pending sections for a single stream — instead of O(N) over the whole
    // registry.  Kept in sync with pending_ by Add / Ack / Remove.
    std::unordered_map<uint64_t, std::set<uint64_t>> by_stream_;
};

}
}

#endif


