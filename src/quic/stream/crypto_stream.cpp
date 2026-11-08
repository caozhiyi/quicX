#include <algorithm>
#include <cstring>
#include <vector>

#include "common/log/log.h"

#include "quic/connection/error.h"
#include "quic/frame/crypto_frame.h"
#include "quic/quicx/global_resource.h"
#include "quic/stream/crypto_stream.h"

namespace quicx {
namespace quic {

CryptoStream::CryptoStream(std::weak_ptr<common::IEventLoop> loop,
    std::function<void(std::shared_ptr<IStream>)> active_send_cb,
    std::function<void(uint64_t stream_id)> stream_close_cb,
    std::function<void(uint64_t error, uint16_t frame_type, const std::string& resion)> connection_close_cb):
    IStream(loop, 0, active_send_cb, stream_close_cb, connection_close_cb) {
    for (int i = 0; i < kNumEncryptionLevels; i++) {
        next_read_offset_[i] = 0;
        out_order_bytes_[i] = 0;
        send_offset_[i] = 0;
        acked_offset_[i] = 0;
        emitted_offset_[i] = 0;
        read_buffers_[i] =
            std::make_shared<common::MultiBlockBuffer>(GlobalResource::Instance().GetThreadLocalBlockPool());
    }
}

CryptoStream::~CryptoStream() {
    // The scheduler holds a raw pointer to this owner; never leave it dangling.
    if (auto sched = retransmit_scheduler_.lock()) {
        sched->RemoveOwner(this);
    }
}

void CryptoStream::SetRetransmitScheduler(std::weak_ptr<RetransmitScheduler> scheduler) {
    retransmit_scheduler_ = std::move(scheduler);
}

void CryptoStream::DisarmSlot(uint8_t level) {
    if (auto sched = retransmit_scheduler_.lock()) {
        sched->Remove(this, level);
    }
}

namespace {

// Disjoint [start, end) interval set with merge-on-insert; just enough for the
// "which parts of this range are not covered yet" question the sent-log asks.
class IntervalSet {
public:
    void Add(uint64_t start, uint64_t end) {
        if (end <= start) {
            return;
        }
        auto it = m_.upper_bound(start);
        if (it != m_.begin()) {
            auto prev = std::prev(it);
            if (prev->second >= start) {
                start = prev->first;
                end = std::max(end, prev->second);
                it = m_.erase(prev);
            }
        }
        while (it != m_.end() && it->first <= end) {
            end = std::max(end, it->second);
            it = m_.erase(it);
        }
        m_.emplace(start, end);
    }

    std::vector<std::pair<uint64_t, uint64_t>> Gaps(uint64_t start, uint64_t end) const {
        std::vector<std::pair<uint64_t, uint64_t>> gaps;
        uint64_t cur = start;
        auto it = m_.upper_bound(start);
        if (it != m_.begin()) {
            auto prev = std::prev(it);
            if (prev->second > cur) {
                cur = prev->second;
            }
        }
        for (; it != m_.end() && it->first < end && cur < end; ++it) {
            if (it->first > cur) {
                gaps.emplace_back(cur, it->first);
            }
            cur = std::max(cur, it->second);
        }
        if (cur < end) {
            gaps.emplace_back(cur, end);
        }
        return gaps;
    }

private:
    std::map<uint64_t, uint64_t> m_;
};

}  // namespace

void CryptoStream::LogSent(uint8_t level, uint64_t start, uint64_t end, uint64_t now_ms) {
    if (end <= start) {
        return;
    }
    auto& log = sent_log_[level];
    log.push_back(SentRange{start, end, now_ms});
    if (log.size() > kMaxSentLogEntries) {
        log.pop_front();
    }
}

std::vector<CryptoStream::SentRange> CryptoStream::LiveRanges(uint8_t level) const {
    // Walk newest -> oldest. Bytes a newer entry already covers were re-sent
    // since, so the older entry's timestamp no longer describes them; bytes the
    // peer acknowledged are not outstanding at all.
    IntervalSet covered;
    covered.Add(0, acked_offset_[level]);
    for (const auto& ar : acked_ranges_[level]) {
        covered.Add(ar.first, ar.second);
    }
    std::vector<SentRange> live;
    const auto& log = sent_log_[level];
    for (auto it = log.rbegin(); it != log.rend(); ++it) {
        for (const auto& gap : covered.Gaps(it->start, it->end)) {
            live.push_back(SentRange{gap.first, gap.second, it->sent_ms});
        }
        covered.Add(it->start, it->end);
    }
    return live;
}

void CryptoStream::RefreshSlotDeadline(uint8_t level) {
    auto sched = retransmit_scheduler_.lock();
    if (!sched || !sched->IsArmed(this, level)) {
        return;
    }
    const auto live = LiveRanges(level);
    if (live.empty()) {
        return;
    }
    uint64_t oldest = UINT64_MAX;
    for (const auto& r : live) {
        oldest = std::min(oldest, r.sent_ms);
    }
    const uint64_t deadline = oldest + sched->IntervalMs();
    // Never pull the deadline into the past: an overdue one means bytes are
    // already queued for a probe that has not gone out yet (cwnd / budget), and
    // the scheduler's own post-fire restart is the right cadence for that.
    if (deadline > sched->Now()) {
        sched->Update(this, level, deadline);
    }
}

void CryptoStream::OnRetransmitDeadline(uint32_t slot, uint64_t now_ms) {
    if (slot >= kNumEncryptionLevels) {
        if (auto sched = retransmit_scheduler_.lock()) {
            sched->Remove(this, slot);
        }
        return;
    }
    const uint8_t level = static_cast<uint8_t>(slot);

    // Nothing the peer could still acknowledge at this level: drop the slot
    // (the scheduler already restarted it before calling us).
    const bool keys_gone = (level == kInitial && initial_consumed_by_peer_) ||
                           (level == kHandshake && handshake_confirmed_);
    if (keys_gone || acked_offset_[level] >= emitted_offset_[level]) {
        DisarmSlot(level);
        return;
    }

    auto sched = retransmit_scheduler_.lock();
    const uint64_t pto = sched ? sched->IntervalMs() : 0;
    const auto live = LiveRanges(level);

    if (live.empty()) {
        // No timing information (sent-log capped away or never recorded): fall
        // back to the whole outstanding window. The gap algorithm in
        // OnFrameDelivery still skips ACKed and already-queued bytes.
        const uint64_t window = emitted_offset_[level] - acked_offset_[level];
        OnFrameDelivery(level, acked_offset_[level],
            static_cast<uint32_t>(std::min<uint64_t>(window, UINT32_MAX)), FrameDeliveryState::kProbe);
        return;
    }

    // Probe only bytes whose LAST transmission is at least one base PTO old.
    // Younger bytes have not had a fair chance to be acknowledged; re-sending
    // them is redundant and, under the 8.1 budget, steals credit from bytes
    // that really are lost. kProbe, not kLost: this is the arithmetic-cadence
    // fallback (lesson L1), never a loss event.
    uint64_t next_due = UINT64_MAX;
    size_t probed = 0;
    for (const auto& r : live) {
        if (r.sent_ms + pto <= now_ms) {
            ++probed;
            OnFrameDelivery(level, r.start, static_cast<uint32_t>(std::min<uint64_t>(r.end - r.start, UINT32_MAX)),
                FrameDeliveryState::kProbe);
        } else {
            next_due = std::min(next_due, r.sent_ms + pto);
        }
    }
    LOG_DEBUG("CryptoStream::OnRetransmitDeadline: level=%d live_ranges=%zu probed=%zu acked_prefix=%llu emitted=%llu",
        level, live.size(), probed, (unsigned long long)acked_offset_[level],
        (unsigned long long)emitted_offset_[level]);

    // The scheduler restarted this slot at now + PTO before calling us. If a
    // younger range falls due sooner than that (or nothing was old enough
    // because the PTO grew since the deadline was set), follow it.
    if (sched && next_due != UINT64_MAX && (probed == 0 || next_due < now_ms + pto)) {
        sched->Update(this, level, next_due);
    }
}

void CryptoStream::RecordAcked(uint8_t level, uint64_t start, uint64_t end) {
    if (end <= start) {
        return;
    }
    auto& m = acked_ranges_[level];
    uint64_t new_start = start;
    uint64_t new_end = end;
    // Merge-insert (same shape as SendStream::OnDataAcked): collapse every
    // neighbour the new interval touches in one pass.
    auto it = m.upper_bound(new_start);
    if (it != m.begin()) {
        auto prev = std::prev(it);
        if (prev->second >= new_start) {
            new_start = prev->first;
            if (prev->second > new_end) new_end = prev->second;
            it = prev;
        }
    }
    while (it != m.end() && it->first <= new_end) {
        if (it->second > new_end) new_end = it->second;
        it = m.erase(it);
    }
    m.emplace(new_start, new_end);

    // Advance the contiguous prefix over whatever is now anchored to it, then
    // forget ranges the prefix subsumes (keeps the map bounded by the window).
    auto first = m.begin();
    if (first->first <= acked_offset_[level] && first->second > acked_offset_[level]) {
        acked_offset_[level] = first->second;
    }
    while (!m.empty() && m.begin()->second <= acked_offset_[level]) {
        m.erase(m.begin());
    }
}

IStream::TrySendResult CryptoStream::TrySendData(IFrameVisitor* visitor, EncryptionLevel level) {
    if (level >= kNumEncryptionLevels) {
        return IStream::TrySendResult::kFailed;
    }

    auto& outgoing = outgoing_[level];
    if (outgoing.empty()) {
        // Bug fix (burst-mode handshake stall):
        // The current encryption level has no pending CRYPTO data, but data for
        // *other* levels (e.g., Handshake EE/Cert/CV/Fin while the caller is
        // still draining Initial) may still be waiting. StreamManager treats
        // kSuccess as "stream finished" and erases it from the active set, so
        // if we silently return here the CryptoStream is dropped even though
        // it still has work pending — the next level switch will never see it,
        // and the handshake stalls until the idle-timeout fires.
        //
        // In the legacy per-packet send path this was masked because each
        // TrySend() re-ran the encryption-level scheduler and the freshly
        // selected level usually matched a non-empty buffer. The burst-mode
        // send path reuses one selected level across multiple build attempts
        // within a single round, which is what exposed the latent bug.
        //
        // Fix: before signalling kSuccess, re-arm ToSend() if any other level
        // still has data, so StreamManager keeps the CryptoStream on the
        // active list for the next round.
        for (uint8_t i = 0; i < kNumEncryptionLevels; i++) {
            if (i != level && !outgoing_[i].empty()) {
                ToSend();
                break;
            }
        }
        return IStream::TrySendResult::kSuccess;
    }

    auto& seg = outgoing.front();
    const uint64_t frame_offset = seg.offset;

    // make crypto frame
    auto frame = std::make_shared<CryptoFrame>();
    frame->SetOffset(frame_offset);
    frame->SetEncryptionLevel(level);

    // Per-datagram cap: cap CRYPTO frame payload by the current packet
    // buffer's real free space. CRYPTO frame header worst case:
    // type(1B) + offset(<=8B) + length(<=2B) = ~11B; reserve 20B for safety.
    constexpr uint32_t kCryptoHeaderReserve = 20;
    uint32_t crypto_pkt_left = visitor->GetPacketLeftSize();
    uint32_t crypto_pkt_cap = crypto_pkt_left > kCryptoHeaderReserve ? crypto_pkt_left - kCryptoHeaderReserve : 0;

    const uint32_t seg_left = static_cast<uint32_t>(seg.end_offset - seg.offset);
    uint32_t write_size = visitor->GetLeftStreamDataSize();
    if (write_size > crypto_pkt_cap) {
        write_size = crypto_pkt_cap;
    }
    if (write_size == 0 || write_size > seg_left) {
        // No stream-level cap declared yet (pre-flow-control handshake
        // bytes), or the cap exceeds what this segment holds: take what fits.
        write_size = std::min(seg_left, crypto_pkt_cap);
    }

    // #1 (code review round 2, P1): when the packet build buffer is already
    // full (crypto_pkt_cap == 0), write_size collapses to 0 here. The old
    // code fell through to the empty-segment branch below and POPPED the
    // segment — permanently dropping handshake bytes with only a DEBUG log,
    // hanging the connection until the idle/handshake timeout. Return kBreak
    // instead: the StreamManager keeps the stream on the active list and the
    // segment is retried in the next packet.
    if (write_size == 0 && seg_left > 0) {
        LOG_INFO("CryptoStream::TrySendData: packet full, deferring segment. level:%d, offset:%llu", level,
            (unsigned long long)seg.offset);
        return IStream::TrySendResult::kBreak;
    }

    common::SharedBufferSpan data(seg.chunk, seg.chunk->GetData() + seg.pos, write_size);
    if (!data.Valid() || data.GetLength() == 0) {
        // Defensive: an empty/invalid segment (seg_left == 0 here, or a bad
        // chunk) should not be queued. Drop the segment and report success so
        // the loop continues with the next.
        LOG_DEBUG("CryptoStream::TrySendData: empty segment, level:%d", level);
        outgoing.pop_front();
        return IStream::TrySendResult::kSuccess;
    }
    frame->SetData(data);

    if (!visitor->HandleFrame(frame)) {
        LOG_WARN("CryptoStream::TrySendData: visitor handle frame failed. level:%d", level);
        return IStream::TrySendResult::kFailed;
    }

    // First-8-bytes hex: runtime self-attestation of what actually goes onto
    // the CRYPTO stream. A 1-RTT CRYPTO payload must start with 0x16 (TLS
    // handshake record, e.g. NewSessionTicket); anything else means the frame
    // already points at the wrong bytes (chunk lifetime / span ownership)
    // before the packet encoder is even involved — the split point between
    // the packet-encoding-layer and stream-layer root-cause branches.
    uint8_t head[8] = {0};
    const uint32_t head_n = data.GetLength() < 8 ? data.GetLength() : 8;
    if (head_n > 0) {
        std::memcpy(head, data.GetStart(), head_n);
    }
    LOG_DEBUG("CryptoStream::TrySendData: sent frame level:%d, offset:%llu, len:%d, "
              "head:%02x%02x%02x%02x%02x%02x%02x%02x",
        level, (unsigned long long)frame_offset, data.GetLength(), head[0], head[1], head[2], head[3], head[4],
        head[5], head[6], head[7]);

    // Frame-level delivery wiring: SendControl fires kAcked/kLost on this
    // frame (see IFrame::SetDeliveryHandler). The handler owns the CRYPTO
    // reliability — a lost range comes back as a fresh outgoing segment that
    // the normal send path sizes to the current budget (the build-time
    // anti-amplification constraint in ComputeSendPolicy), instead of the
    // old full-datagram re-encode that never fit a starved budget.
    const uint32_t sent_len = data.GetLength();
    auto weak_self = weak_from_this();
    frame->SetDeliveryHandler([weak_self, level, frame_offset, sent_len](FrameDeliveryState state) {
        if (auto self = weak_self.lock()) {
            if (auto cs = std::dynamic_pointer_cast<CryptoStream>(self)) {
                cs->OnFrameDelivery(level, frame_offset, sent_len, state);
            }
        }
    });

    seg.pos += sent_len;
    seg.offset += sent_len;
    if (seg.offset >= seg.end_offset) {
        outgoing.pop_front();
    }

    // P0-C10 / X3: track the outstanding window and (for the handshake
    // levels) arm the slow-path slot. Arm is idempotent and keeps an earlier
    // deadline, so a busy sender never pushes the retransmit of its oldest
    // outstanding bytes out.
    emitted_offset_[level] = std::max<uint64_t>(emitted_offset_[level], frame_offset + sent_len);
    const bool level_usable = (level == kInitial && !initial_consumed_by_peer_) ||
                              (level == kHandshake && !handshake_confirmed_);
    if (level_usable) {
        if (auto sched = retransmit_scheduler_.lock()) {
            LogSent(level, frame_offset, frame_offset + sent_len, sched->Now());
            sched->Arm(this, level);
        }
    }

    // Check if we still have data for this level
    if (!outgoing.empty()) {
        ToSend();
        return IStream::TrySendResult::kSuccess;
    }

    // Check if we have data for other levels
    for (uint8_t i = 0; i < kNumEncryptionLevels; i++) {
        if (i != level && !outgoing_[i].empty()) {
            ToSend();
            break;
        }
    }

    return IStream::TrySendResult::kSuccess;
}

// reset the stream
void CryptoStream::Reset(uint32_t /*error*/) {
    // do nothing
}

void CryptoStream::ResetForRetry() {
    LOG_INFO("Resetting CryptoStream for Retry");

    // Clear Initial level state to restart handshake from offset 0
    uint8_t level = kInitial;

    // Reset read state
    read_buffers_[level] =
        std::make_shared<common::MultiBlockBuffer>(GlobalResource::Instance().GetThreadLocalBlockPool());
    next_read_offset_[level] = 0;
    out_order_frame_[level].clear();
    out_order_bytes_[level] = 0;

    // Reset send state (frame-delivery model): the Initial-level flight is
    // dead after a Retry; TLS regenerates it from offset 0.
    outgoing_[level].clear();
    retained_[level].clear();
    acked_offset_[level] = 0;
    acked_ranges_[level].clear();
    emitted_offset_[level] = 0;
    send_offset_[level] = 0;
    DisarmSlot(level);
    sent_log_[level].clear();
}

void CryptoStream::Close() {
    // do nothing
}

uint32_t CryptoStream::OnFrame(std::shared_ptr<IFrame> frame) {
    uint16_t frame_type = frame->GetType();
    if (frame_type == FrameType::kCrypto) {
        OnCryptoFrame(frame);
        return 0;
    }
    // shouldn't be here
    LOG_ERROR("crypto stream recv error frame. type:%d", frame_type);
    return 0;
}

int32_t CryptoStream::Send(uint8_t* data, uint32_t len, uint8_t encryption_level) {
    auto loop = event_loop_.lock();
    if (!loop) return -1;
    if (!loop->IsInLoopThread()) {
        std::vector<uint8_t> vec(data, data + len);
        auto weak_self = weak_from_this();
        loop->RunInLoop([weak_self, vec = std::move(vec), encryption_level]() {
            auto self = weak_self.lock();
            if (!self) return;
            auto stream = std::dynamic_pointer_cast<CryptoStream>(self);
            if (stream) stream->Send(const_cast<uint8_t*>(vec.data()), vec.size(), encryption_level);
        });
        return len;
    }

    // Frame-delivery model: copy the bytes into pool chunks that stay alive
    // via shared_ptr (the old MoveReadPt consumption model recycled the
    // blocks, which made re-reads for retransmission impossible). Each chunk
    // becomes one retained + one outgoing segment; oversized Send() calls
    // simply span several chunks with consecutive offsets.
    uint32_t remaining = len;
    uint8_t* src = data;
    while (remaining > 0) {
        auto chunk = std::make_shared<common::BufferChunk>(GlobalResource::Instance().GetThreadLocalBlockPool());
        if (!chunk || !chunk->Valid()) {
            // A dropped chunk is a gap the peer can never fill: the flight
            // would stall until the idle timeout. Loudly fatal-adjacent so it
            // is never missed.
            LOG_ERROR("CryptoStream::Send PARTIAL WRITE: handshake data dropped. level=%d want=%u wrote=%u",
                encryption_level, len, len - remaining);
            return (int32_t)(len - remaining);
        }
        const uint32_t n = std::min(remaining, chunk->GetLength());
        std::memcpy(chunk->GetData(), src, n);
        const uint64_t seg_offset = send_offset_[encryption_level];
        LOG_DEBUG("CryptoStream::Send: retain level=%d offset=%llu len=%u",
            encryption_level, (unsigned long long)seg_offset, n);
        retained_[encryption_level].push_back(RetainedSegment{seg_offset, n, chunk});
        outgoing_[encryption_level].push_back(OutgoingSegment{seg_offset, seg_offset + n, 0, chunk});
        send_offset_[encryption_level] += n;
        src += n;
        remaining -= n;
    }

    ToSend();
    return (int32_t)len;
}

int32_t CryptoStream::Send(uint8_t* data, uint32_t len) {
    auto loop = event_loop_.lock();
    if (!loop) return -1;
    if (!loop->IsInLoopThread()) {
        std::vector<uint8_t> vec(data, data + len);
        auto weak_self = weak_from_this();
        loop->RunInLoop([weak_self, vec = std::move(vec)]() {
            auto self = weak_self.lock();
            if (!self) return;
            auto stream = std::dynamic_pointer_cast<CryptoStream>(self);
            if (stream) stream->Send(const_cast<uint8_t*>(vec.data()), vec.size());
        });
        return len;
    }

    return Send(data, len, GetWaitSendEncryptionLevel());
}

int32_t CryptoStream::Send(std::shared_ptr<IBufferRead> data) {
    auto loop = event_loop_.lock();
    if (!loop) return -1;
    if (!loop->IsInLoopThread()) {
        auto weak_self = weak_from_this();
        loop->RunInLoop([weak_self, data]() {
            auto self = weak_self.lock();
            if (!self) return;
            auto stream = std::dynamic_pointer_cast<CryptoStream>(self);
            if (stream) stream->Send(data);
        });
        return data->GetDataLength();
    }

    // Frame-delivery model: flatten the readable bytes into one contiguous
    // scratch copy, then feed the chunking Send(). (No production caller
    // passes a multi-chunk buffer here today — TLS hands us raw pointers —
    // but the interface is public, so handle it correctly.)
    uint8_t level = GetWaitSendEncryptionLevel();
    const uint32_t len = data->GetDataLength();
    if (len == 0) {
        return 0;
    }
    std::vector<uint8_t> scratch(len);
    const uint32_t got = data->ReadNotMovePt(scratch.data(), len);
    if (got == 0) {
        return 0;
    }
    return Send(scratch.data(), got, level);
}

uint8_t CryptoStream::GetWaitSendEncryptionLevel() {
    uint8_t level = kApplication;
    if (!outgoing_[kInitial].empty()) {
        level = kInitial;
    } else if (!outgoing_[kHandshake].empty()) {
        level = kHandshake;
    }
    return level;
}

void CryptoStream::OnCryptoFrame(std::shared_ptr<IFrame> frame) {
    auto crypto_frame = std::dynamic_pointer_cast<CryptoFrame>(frame);
    // CRITICAL: Use the level from the frame to select correct state
    // FrameProcessor/Connection layer MUST ensure this level is set
    uint8_t level = crypto_frame->GetEncryptionLevel();

    // Bounds check
    if (level >= kNumEncryptionLevels) {
        LOG_ERROR("CryptoStream received frame with invalid level %d", level);
        return;
    }

    LOG_INFO("CryptoStream::OnCryptoFrame: level=%d, offset=%llu, len=%u, expected=%llu", level,
        crypto_frame->GetOffset(), crypto_frame->GetLength(), next_read_offset_[level]);

    const uint64_t frame_offset = crypto_frame->GetOffset();
    const uint64_t frame_length = crypto_frame->GetLength();

    // RFC 9000 §19.6: "The largest offset delivered on a stream -- the sum of the
    // offset and data length -- cannot exceed 2^62-1 [...] receipt of a frame that
    // exceeds this limit MUST be treated as a connection error of type
    // FRAME_ENCODING_ERROR." This also covers the unsigned wraparound of
    // offset + length that would otherwise corrupt next_read_offset_.
    if (frame_offset > kMaxCryptoOffset || frame_length > kMaxCryptoOffset - frame_offset) {
        LOG_ERROR("crypto frame offset+length out of range. level:%d, offset:%llu, len:%llu", level, frame_offset,
            frame_length);
        if (connection_close_cb_) {
            connection_close_cb_(
                QuicErrorCode::kFrameEncodingError, frame->GetType(), "crypto frame offset+length out of range.");
        }
        return;
    }

    if (frame_offset <= next_read_offset_[level]) {
        // In-order, or overlapping data we have already delivered in part. Skip the
        // prefix we have seen and append only the new suffix; a pure duplicate
        // contributes nothing.
        const uint64_t already_have = next_read_offset_[level] - frame_offset;
        if (already_have < frame_length) {
            // IMPORTANT: Copy the bytes into read_buffers_ (do NOT push the shared
            // chunk). CRYPTO frames from a received packet share the packet's
            // decode buffer; that buffer is recycled once the packet is fully
            // processed, so holding a shared pointer is not enough to guarantee
            // the bytes remain stable. Copy into a fresh chunk owned by
            // read_buffers_[level] to avoid later corruption.
            auto data_span = crypto_frame->GetData();
            const uint32_t new_bytes = static_cast<uint32_t>(frame_length - already_have);
            read_buffers_[level]->Write(data_span.GetStart() + already_have, new_bytes);
            next_read_offset_[level] += new_bytes;
        }

        // Buffered frames may now be contiguous (possibly overlapping) with the
        // advanced offset.
        DrainOutOrderFrames(level);

        // Notify upper layer (TLS) with correct level
        if (recv_cb_) {
            recv_cb_(read_buffers_[level], 0, level);
        }
        return;
    }

    // Strictly ahead of the current offset: buffer it until the gap is filled.
    // Duplicates at the same offset are ignored so an existing (possibly longer)
    // buffered frame is not overwritten.
    auto& out_order = out_order_frame_[level];
    if (out_order.find(frame_offset) != out_order.end()) {
        return;
    }

    // RFC 9000 §7.5 cap. Enforced before allocating, so the attacker-controlled
    // frame cannot cause the allocation it is meant to prevent.
    if (out_order.size() >= kMaxCryptoOutOfOrderFrames || out_order_bytes_[level] + frame_length > kMaxCryptoOutOfOrderBytes) {
        LOG_ERROR("crypto out-of-order buffer exceeded. level:%d, frames:%zu, bytes:%llu, incoming:%llu", level,
            out_order.size(), out_order_bytes_[level], frame_length);
        if (connection_close_cb_) {
            connection_close_cb_(
                QuicErrorCode::kCryptoBufferExceeded, frame->GetType(), "crypto out-of-order buffer exceeded.");
        }
        return;
    }

    // Must also detach from packet buffer: copy into a standalone frame.
    auto data_span = crypto_frame->GetData();
    auto new_frame = std::make_shared<CryptoFrame>();
    new_frame->SetOffset(frame_offset);
    new_frame->SetEncryptionLevel(level);
    // Allocate a dedicated buffer and copy bytes so the span stays valid
    // after the source packet buffer is recycled.
    auto standalone = std::make_shared<common::MultiBlockBuffer>(GlobalResource::Instance().GetThreadLocalBlockPool());
    standalone->Write(data_span.GetStart(), static_cast<uint32_t>(frame_length));
    auto owned_span = standalone->GetSharedReadableSpan(static_cast<uint32_t>(frame_length));
    new_frame->SetData(owned_span);
    out_order[frame_offset] = new_frame;
    out_order_bytes_[level] += frame_length;
}

uint64_t CryptoStream::DrainOutOrderFrames(uint8_t level) {
    auto& out_order = out_order_frame_[level];
    uint64_t appended = 0;

    while (!out_order.empty()) {
        // Largest buffered offset that is not beyond the delivery point. Anything
        // above it leaves a genuine gap, so delivery stops there.
        auto upper = out_order.upper_bound(next_read_offset_[level]);
        if (upper == out_order.begin()) {
            break;
        }
        auto iter = std::prev(upper);

        auto buffered = std::dynamic_pointer_cast<CryptoFrame>(iter->second);
        if (!buffered) {
            out_order.erase(iter);
            continue;
        }

        const uint64_t buffered_offset = iter->first;
        const uint64_t buffered_length = buffered->GetLength();
        const uint64_t already_have = next_read_offset_[level] - buffered_offset;

        if (already_have < buffered_length) {
            auto span = buffered->GetData();
            const uint32_t new_bytes = static_cast<uint32_t>(buffered_length - already_have);
            read_buffers_[level]->Write(span.GetStart() + already_have, new_bytes);
            next_read_offset_[level] += new_bytes;
            appended += new_bytes;
        }
        // Fully superseded (or now consumed): release it either way.
        out_order_bytes_[level] -= std::min(out_order_bytes_[level], buffered_length);
        out_order.erase(iter);
    }

    return appended;
}

void CryptoStream::MarkInitialConsumedByPeer() {
    if (initial_consumed_by_peer_) {
        return;
    }
    initial_consumed_by_peer_ = true;
    // The peer cannot acknowledge this flight any more (Initial keys are
    // gone); keeping the segments would only re-queue them on every PTO.
    outgoing_[kInitial].clear();
    retained_[kInitial].clear();
    acked_ranges_[kInitial].clear();
    DisarmSlot(kInitial);
    sent_log_[kInitial].clear();
    LOG_INFO("CryptoStream::MarkInitialConsumedByPeer: dropping Initial send state, peer already consumed the flight");
}

void CryptoStream::MarkHandshakeConfirmed() {
    if (handshake_confirmed_) {
        return;
    }
    handshake_confirmed_ = true;
    // RFC 9001 4.9.1: handshake keys are discarded at confirmation. The peer
    // that already confirmed on its side dropped its handshake keys and can
    // never decrypt — let alone ACK — anything we still hold at this level.
    // Retransmission is therefore pointless and is stopped two ways: the
    // retained (already-sent) segments are dropped here, and OnFrameDelivery
    // ignores further kLost at this level so nothing re-queues again — that
    // re-queue loop was the ~200pps eternal Handshake PING+CRYPTO probe storm
    // in the crosstraffic interop failure.
    //
    // outgoing_ (bytes queued but NOT yet packed into a packet) is
    // deliberately KEPT: the confirmation signal can overtake our own flight.
    // The client's Finished is queued by the TLS stack while processing the
    // server's Handshake flight, and the very next server datagram (1-RTT
    // frames — NEW_CONNECTION_ID, NST, even HANDSHAKE_DONE) triggers the
    // confirmation path BEFORE the Finished ever left. Dropping it deadlocks
    // the handshake: the server never sees the Finished, never completes TLS,
    // and the connection dies (connection_close/resumption unit tests). Those
    // bytes get exactly one final transmission attempt; their loss is then
    // final, which the peer's dropped keys make correct.
    retained_[kHandshake].clear();
    acked_ranges_[kHandshake].clear();
    DisarmSlot(kHandshake);
    sent_log_[kHandshake].clear();
    LOG_INFO("CryptoStream::MarkHandshakeConfirmed: dropping Handshake retransmit state, handshake confirmed "
             "(outgoing kept: %zu segments)",
        outgoing_[kHandshake].size());
}

void CryptoStream::OnFrameDelivery(uint8_t level, uint64_t offset, uint32_t length, FrameDeliveryState state) {
    if (level >= kNumEncryptionLevels) {
        return;
    }

    if (state == FrameDeliveryState::kAcked) {
        // Continuous-prefix semantics. Only a segment that starts at or
        // before the current prefix end may extend the prefix; an ACK for a
        // segment beyond a gap (start > prefix end) must be ignored for the
        // prefix. With the old max() advance, a Handshake flight where
        // segment N was lost reported segment N+1 as "acked", the prefix
        // jumped the gap, and the later kLost for segment N was then
        // swallowed by the "offset + length <= acked_offset_" check below
        // (and its retained chunk was pruned): the lost CRYPTO bytes were
        // never retransmitted and the peer's handshake timed out
        // (amplificationlimit interop, 8/14 clients failing).
        //
        // The ACKed range itself is still remembered (acked_ranges_) so a
        // later requeue of the surrounding window skips it; only the PREFIX
        // advances over contiguous coverage (RecordAcked).
        RecordAcked(level, offset, offset + length);
        // Everything emitted at this level is acknowledged: nothing left for
        // the slow-path slot to retransmit.
        if (acked_offset_[level] >= emitted_offset_[level]) {
            DisarmSlot(level);
            sent_log_[level].clear();
        } else {
            // Drop log entries the prefix has swallowed, then let the deadline
            // follow the OLDEST still-unacknowledged bytes: after a partial ACK
            // the first send's time no longer describes what is outstanding.
            auto& log = sent_log_[level];
            while (!log.empty() && log.front().end <= acked_offset_[level]) {
                log.pop_front();
            }
            RefreshSlotDeadline(level);
        }
        // A late ACK (the packet was already declared lost and its range
        // re-queued) retires the re-queued segment so we stop re-sending.
        PruneAckedSegments(level);
        // #2 (code review round 2, P2): trim the fully-acked prefix of
        // retained_. Only kInitial ever had explicit clears, so on
        // Handshake/1-RTT every retained chunk — one pool block per Send()
        // slice, e.g. every NewSessionTicket on a long-lived connection —
        // stayed alive forever. Segments are pushed in strictly increasing
        // offset order (Send() advances send_offset_), so trimming from the
        // front bounds the retained set to the un-acked window.
        auto& retained = retained_[level];
        while (!retained.empty() && retained.front().offset + retained.front().length <= acked_offset_[level]) {
            retained.pop_front();
        }
        return;
    }

    // P0-C10: kLost and kProbe share the re-queue path below. kProbe (PTO
    // probe / retransmit-scheduler cadence) is NOT a loss event — the
    // congestion reaction is SendControl's business, and kProbe never reaches
    // here from OnPacketLost — but the owner's action is identical: put the
    // still-unacknowledged bytes back on the wire. The gap algorithm makes
    // repeated probes idempotent (already-queued ranges yield no gaps).
    //
    // kLost, but already covered by the acked prefix: superseded by a late
    // ACK (notifications can arrive out of order — OnPacketAck fires kAcked
    // on packets that were already declared lost). Ignore.
    if (offset + length <= acked_offset_[level]) {
        return;
    }

    // Initial-level loss after the peer consumed our Initial flight: the peer
    // has dropped its Initial keys and can never ACK this range. Re-queueing
    // would spin the PTO forever (quinn handshakecorruption: [0,733) re-queued
    // every 15 s until the handshake timeout).
    if (level == kInitial && initial_consumed_by_peer_) {
        return;
    }

    // Handshake-level loss after handshake confirmation: the peer dropped its
    // handshake keys at its own confirmation point (RFC 9001 4.9.1), so this
    // range can never be ACKed. Re-queueing would spin a no-backoff PTO loop
    // for the rest of the connection (crosstraffic: ~200pps Handshake
    // PING+CRYPTO probes starving the server's cwnd to its floor against 8
    // servers, 25MB download stuck at ~10.7MB).
    if (level == kHandshake && handshake_confirmed_) {
        return;
    }

    // #4 (code review round 2): only re-queue the parts of [offset, range_end)
    // NOT already covered by a queued segment's unread remainder. The old
    // full-containment check (IsRangeQueued) let a partially-overlapping range
    // re-queue its already-queued prefix too — duplicated CRYPTO bytes are
    // legal (the peer reassembles by offset) but pure waste under the §8.1
    // budget, and it also served as the PTO idempotence guard, which the
    // difference below subsumes: a fully-covered range yields no gaps.
    const uint64_t range_end = offset + length;
    std::vector<std::pair<uint64_t, uint64_t>> gaps;  // uncovered sub-ranges of the lost range
    {
        // Queued unread intervals; may be out of order (re-queues push_back),
        // so collect then sort before subtracting.
        std::vector<std::pair<uint64_t, uint64_t>> queued;
        queued.reserve(outgoing_[level].size());
        for (const auto& qseg : outgoing_[level]) {
            if (qseg.end_offset > qseg.offset) {
                queued.emplace_back(qseg.offset, qseg.end_offset);
            }
        }
        // Ranges already ACKed beyond the contiguous prefix are just as
        // covered as queued ones (overlaps between the two sources are fine —
        // the subtraction below advances `cur` monotonically).
        for (const auto& acked : acked_ranges_[level]) {
            if (acked.second > offset && acked.first < offset + length) {
                queued.emplace_back(acked.first, acked.second);
            }
        }
        std::sort(queued.begin(), queued.end());
        uint64_t cur = offset;
        for (const auto& q : queued) {
            if (q.first >= range_end || cur >= range_end) {
                break;
            }
            if (q.first > cur) {
                gaps.emplace_back(cur, q.first);
            }
            if (q.second > cur) {
                cur = q.second;
            }
        }
        if (cur < range_end) {
            gaps.emplace_back(cur, range_end);
        }
    }
    if (gaps.empty()) {
        return;  // fully covered by queued segments (PTO double-fire); nothing to re-queue
    }

    // Re-queue each uncovered gap from the retained chunks. Frames are carved
    // out of single retained segments in the common case; the inner loop
    // handles a frame straddling a chunk boundary defensively.
    for (const auto& gap : gaps) {
        uint64_t cur = gap.first;
        for (const auto& r : retained_[level]) {
            if (cur >= gap.second) {
                break;
            }
            const uint64_t r_start = r.offset;
            // The segment's real extent is the bytes Send() wrote, NOT the chunk
            // capacity — see RetainedSegment for the bug this field fixes.
            const uint64_t r_end = r.offset + r.length;
            if (cur >= r_end || r_start >= gap.second) {
                continue;  // no overlap with the remaining gap
            }
            const uint64_t start_in_r = (cur > r_start) ? cur : r_start;
            const uint64_t end_in_r = (gap.second < r_end) ? gap.second : r_end;
            // #3: offset/end_offset are absolute CRYPTO offsets; pos is the
            // gap's start measured from the CHUNK's start (start_in_r -
            // r_start), so the span points at the right bytes without the old
            // offset-pos implicit invariant. (Writing end_in_r - start_in_r
            // as pos — the pre-#3 footgun — underflowed seg_left in
            // TrySendData and put uninitialized pool memory on the wire.)
            outgoing_[level].push_back(
                OutgoingSegment{start_in_r, end_in_r, (uint32_t)(start_in_r - r_start), r.chunk});
            cur = end_in_r;
        }
        if (cur < gap.second) {
            // Gap beyond retained data: the segments were cleared (Retry
            // reset) or the frame predates the retained window. Nothing to
            // re-send — TLS owns regenerating those bytes if they still matter.
            LOG_WARN("CryptoStream::OnFrameDelivery: lost range [%llu,%llu) not fully retained at level=%d",
                (unsigned long long)gap.first, (unsigned long long)gap.second, level);
        }
    }

    LOG_DEBUG("CryptoStream::OnFrameDelivery: re-queued lost range [%llu,%llu) at level=%d",
        (unsigned long long)offset, (unsigned long long)range_end, level);
    ToSend();
}

void CryptoStream::PruneAckedSegments(uint8_t level) {
    auto& outgoing = outgoing_[level];
    for (auto it = outgoing.begin(); it != outgoing.end();) {
        if (it->end_offset <= acked_offset_[level]) {
            it = outgoing.erase(it);
        } else {
            ++it;
        }
    }
}

}  // namespace quic
}  // namespace quicx