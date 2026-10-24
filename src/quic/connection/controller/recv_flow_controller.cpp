#include <climits>

#include "common/log/log.h"

#include "quic/config.h"
#include "quic/connection/controller/recv_flow_controller.h"
#include "quic/frame/max_data_frame.h"
#include "quic/frame/max_streams_frame.h"

namespace quicx {
namespace quic {

namespace {
// Fallback growth step when UpdateConfig() was never called (initial window
// unknown). 1MiB keeps grants conservative rather than granting nothing.
constexpr uint64_t kDefaultConnectionWindowIncrement = 1024 * 1024;
}  // namespace

RecvFlowController::RecvFlowController(StreamIDGenerator::StreamStarter local_starter):
    received_bytes_(0),
    max_data_(0),
    initial_max_data_(0),
    max_streams_bidi_(0),
    max_streams_uni_(0),
    max_bidi_stream_id_(0),
    max_uni_stream_id_(0),
    local_starter_(local_starter) {}

void RecvFlowController::UpdateConfig(const TransportParam& tp) {
    max_data_ = tp.GetInitialMaxData();
    initial_max_data_ = max_data_;
    max_streams_bidi_ = tp.GetInitialMaxStreamsBidi();
    max_streams_uni_ = tp.GetInitialMaxStreamsUni();

    LOG_DEBUG("RecvFlowController::UpdateConfig: max_data=%llu, max_streams_bidi=%llu, max_streams_uni=%llu", max_data_,
        max_streams_bidi_, max_streams_uni_);
}

bool RecvFlowController::OnDataReceived(uint32_t size) {
    received_bytes_ += size;
    LOG_DEBUG("RecvFlowController::OnDataReceived: received %u bytes, total=%llu, limit=%llu", size, received_bytes_,
        max_data_);

    // Check if peer exceeded our limit (protocol violation)
    if (received_bytes_ > max_data_) {
        LOG_ERROR("RecvFlowController::OnDataReceived: peer exceeded MAX_DATA limit (received=%llu, limit=%llu)",
            received_bytes_, max_data_);
        return false;
    }

    return true;
}

bool RecvFlowController::ShouldSendMaxData(std::shared_ptr<IFrame>& max_data_frame) {
    // Check if peer violated limit
    if (received_bytes_ > max_data_) {
        LOG_ERROR("RecvFlowController::ShouldSendMaxData: peer exceeded limit");
        return false;
    }

    // Check if we should increase the limit (peer is near the window)
    // Use percentage-based threshold: trigger when less than 10% remaining
    uint64_t remaining = max_data_ - received_bytes_;
    uint64_t threshold = max_data_ / 10;  // 10% of max_data

    if (remaining <= threshold) {
        // P0-S2: connection-level receive-memory budget. Pinned receive
        // memory (ordered buffers + out-of-order reassembly across all
        // streams) at or above 2x the current window — minimum 8 MiB —
        // means the application is not keeping up. Stop growing the window
        // rather than acknowledging-and-buffering unboundedly; the peer is
        // flow-control blocked, which is exactly the backpressure the
        // window exists to provide. Data already received keeps its ACKs
        // (dropping it would violate the protocol); only growth stops.
        if (buffered_bytes_getter_) {
            const uint64_t budget = std::max<uint64_t>(2 * max_data_, 8ull * 1024 * 1024);
            const uint64_t pinned = buffered_bytes_getter_();
            if (pinned >= budget) {
                LOG_DEBUG(
                    "RecvFlowController::ShouldSendMaxData: receive budget %llu pinned, holding window at %llu",
                    pinned, max_data_);
                return true;  // window intentionally not raised
            }
        }

        // Grow the window LINEARLY by the initial window size (P0-S1). The
        // old code doubled max_data_ each time — with received_bytes_ only
        // ever increasing (no consumption feedback yet) the window grew
        // exponentially to UINT64_MAX, i.e. connection-level flow control
        // silently stopped existing. Linear growth keeps granting capacity
        // while keeping the advertised window proportional to what we have
        // actually absorbed. A full consumption-driven window (RFC 9000 §4.2
        // "implementations ... decide when ... MAX_DATA") is tracked for a
        // later phase.
        uint64_t step = initial_max_data_ > 0 ? initial_max_data_ : kDefaultConnectionWindowIncrement;
        if (max_data_ > UINT64_MAX - step) {
            max_data_ = UINT64_MAX;  // cap instead of wrapping
        } else {
            max_data_ += step;
        }

        auto frame = std::make_shared<MaxDataFrame>();
        frame->SetMaximumData(max_data_);
        max_data_frame = frame;

        LOG_DEBUG(
            "RecvFlowController::ShouldSendMaxData: increasing limit to %llu (remaining was %llu, threshold=%llu)",
            max_data_, remaining, threshold);
    }

    return true;
}

bool RecvFlowController::OnStreamCreated(uint64_t stream_id, std::shared_ptr<IFrame>& max_streams_frame) {
    // RFC 9000 §4.6: MAX_STREAMS limits the number of streams that the PEER
    // can open. It must NOT account for streams we opened ourselves.
    //
    // Stream ID encoding (RFC 9000 §2.1):
    //   bit 0: 0 = client-initiated, 1 = server-initiated
    //   bit 1: 0 = bidirectional,    1 = unidirectional
    //
    // OnStreamCreated is invoked from the receive path whenever a frame
    // arrives that targets a stream the connection has not seen before --
    // either because the peer just opened it, or because a frame arrived
    // for a locally-initiated stream after that stream was already torn
    // down (e.g. an ACK or a reordered/retransmitted response on a bidi
    // stream we opened, looked up after the stream entry was removed).
    //
    // Without distinguishing self- from peer-initiated stream IDs the
    // latter case would consume our advertised MAX_STREAMS budget,
    // eventually causing CheckBidiStreamLimit / CheckUniStreamLimit to
    // return false and triggering a STREAM_LIMIT_ERROR connection close
    // -- a violation of RFC 9000 §4.6 that only manifests under load.
    bool stream_is_server_initiated = (stream_id & 0x01) != 0;
    bool stream_is_local = (local_starter_ == StreamIDGenerator::kServer) == stream_is_server_initiated;
    if (stream_is_local) {
        // Locally-initiated: not subject to the MAX_STREAMS budget we
        // advertised to the peer. Nothing to validate, no MAX_STREAMS to
        // emit.
        return true;
    }

    // Determine stream direction from stream ID (bit 1: 0=bidirectional, 1=unidirectional)
    bool is_unidirectional = (stream_id & 0x02) != 0;

    if (is_unidirectional) {
        // Track highest unidirectional stream ID
        if (stream_id > max_uni_stream_id_) {
            max_uni_stream_id_ = stream_id;
        }
        return CheckUniStreamLimit(max_streams_frame);
    } else {
        // Track highest bidirectional stream ID
        if (stream_id > max_bidi_stream_id_) {
            max_bidi_stream_id_ = stream_id;
        }
        return CheckBidiStreamLimit(max_streams_frame);
    }
}

bool RecvFlowController::CheckBidiStreamLimit(std::shared_ptr<IFrame>& max_streams_frame) {
    // Convert stream ID to stream count (stream ID >> 2 gives the stream number)
    uint64_t current_stream_count = max_bidi_stream_id_ >> 2;

    // Check if peer exceeded our limit (protocol violation)
    // RFC 9000: MAX_STREAMS is the count, so if limit is 10, we can have streams 0-9
    if (current_stream_count >= max_streams_bidi_) {
        LOG_ERROR(
            "RecvFlowController::CheckBidiStreamLimit: peer exceeded MAX_STREAMS_BIDI limit (count=%llu, limit=%llu)",
            current_stream_count, max_streams_bidi_);
        return false;
    }

    // MAX_STREAMS is a CUMULATIVE cap (RFC 9000 §4.6), so the grant must keep
    // the cumulative headroom positive regardless of how many streams closed.
    if (max_streams_bidi_ - current_stream_count <= kStreamsIncreaseThreshold) {
        max_streams_bidi_ = current_stream_count + kStreamsIncreaseAmount + 1;

        auto frame = std::make_shared<MaxStreamsFrame>(FrameType::kMaxStreamsBidirectional);
        frame->SetMaximumStreams(max_streams_bidi_);
        max_streams_frame = frame;

        LOG_DEBUG("RecvFlowController::CheckBidiStreamLimit: increasing limit to %llu (opened=%llu)", max_streams_bidi_,
            current_stream_count);
    }

    return true;
}

bool RecvFlowController::CheckUniStreamLimit(std::shared_ptr<IFrame>& max_streams_frame) {
    // Convert stream ID to stream count
    uint64_t current_stream_count = max_uni_stream_id_ >> 2;

    // Check if peer exceeded our limit (protocol violation)
    // RFC 9000: MAX_STREAMS is the count, so if limit is 10, we can have streams 0-9
    if (current_stream_count >= max_streams_uni_) {
        LOG_ERROR(
            "RecvFlowController::CheckUniStreamLimit: peer exceeded MAX_STREAMS_UNI limit (count=%llu, limit=%llu)",
            current_stream_count, max_streams_uni_);
        return false;
    }

    // Same cumulative-headroom rule as the bidirectional variant.
    if (max_streams_uni_ - current_stream_count <= kStreamsIncreaseThreshold) {
        max_streams_uni_ = current_stream_count + kStreamsIncreaseAmount + 1;

        auto frame = std::make_shared<MaxStreamsFrame>(FrameType::kMaxStreamsUnidirectional);
        frame->SetMaximumStreams(max_streams_uni_);
        max_streams_frame = frame;

        LOG_DEBUG("RecvFlowController::CheckUniStreamLimit: increasing limit to %llu (opened=%llu)", max_streams_uni_,
            current_stream_count);
    }

    return true;
}

}  // namespace quic
}  // namespace quicx
