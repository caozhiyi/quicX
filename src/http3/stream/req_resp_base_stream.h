#ifndef HTTP3_STREAM_REQ_RESP_BASE_STREAM
#define HTTP3_STREAM_REQ_RESP_BASE_STREAM

#include <deque>
#include <functional>
#include <memory>
#include <unordered_map>

#include "http3/frame/frame_decoder.h"
#include "http3/frame/if_frame.h"
#include <quicx/http3/type.h>
#include "http3/qpack/blocked_registry.h"
#include "http3/qpack/qpack_encoder.h"
#include "http3/stream/if_stream.h"
#include <quicx/quic/if_quic_bidirection_stream.h>

namespace quicx {
namespace common {
class QlogTrace;
}
namespace http3 {

/**
 * @brief ReqRespBaseStream is the base class for all request and response
 * streams
 *
 * The req resp base stream is used to handle the request and response frames.
 * It is responsible for handling the headers and data frames.
 */
class ReqRespBaseStream: public IStream, public std::enable_shared_from_this<ReqRespBaseStream> {
public:
    ReqRespBaseStream(const std::shared_ptr<QpackEncoder>& qpack_encoder,
        const std::shared_ptr<QpackEncoder>& qpack_decoder,
        const std::shared_ptr<QpackBlockedRegistry>& blocked_registry,
        const std::shared_ptr<IQuicBidirectionStream>& stream,
        const std::function<void(uint64_t stream_id, uint32_t error_code)>& error_handler);
    virtual ~ReqRespBaseStream();

    virtual uint64_t GetStreamID() override { return stream_->GetStreamID(); }

    // Must be called after construction to set up callbacks
    // Cannot be called in constructor because shared_from_this() requires object to be managed by shared_ptr
    void Init();

    // Set qlog trace for HTTP/3 frame events
    void SetQlogTrace(std::shared_ptr<common::QlogTrace> trace);

    virtual void OnData(std::shared_ptr<IBufferRead> data, bool is_last, uint32_t error);

protected:
    virtual void HandleHeaders(std::shared_ptr<IFrame> frame);
    virtual void HandleData(std::shared_ptr<IFrame> frame);

    virtual void HandleFrame(std::shared_ptr<IFrame> frame);

    virtual void HandleHeaders() = 0;

    // handle the data received callback
    virtual void HandleData(const std::shared_ptr<common::IBuffer>& data, bool is_last) = 0;

    // Send request body using provider (streaming mode)
    bool SendBodyWithProvider(const body_provider& provider);
    bool SendBodyDirectly(const std::shared_ptr<common::IBuffer>& body);
    bool SendHeaders(const std::unordered_map<std::string, std::string>& headers);
    // handle the data sent callback
    void HandleSent(uint32_t length, uint32_t error);

    // Override in subclasses to control whether stream completion should be signaled
    // after all body data is sent.
    // - ResponseStream returns true (server sends response, then stream is done)
    // - RequestStream returns false (client sends request, then waits for response)
    virtual bool ShouldSignalCompletionAfterSend() const { return false; }

    // Called when FIN is received but no HTTP/3 frames were decoded
    // Subclasses should implement this to notify handlers of stream completion
    virtual void HandleFinWithoutData() = 0;

private:
    // Drive the per-frame dispatch loop. Splits |frames| into a sequence of
    // HandleFrame() calls, but stops as soon as a HEADERS frame fails to
    // decode (RFC 9204: blocked on Required Insert Count). Any frames that
    // could not be processed yet are stashed into pending_blocked_frames_
    // along with the trailing is_last marker so they replay in the original
    // order once the QPACK decoder unblocks. This is the production fix for
    // the previous crash path where a DATA frame following a blocked
    // HEADERS frame would dereference a still-null response_/request_.
    void ProcessFrames(std::vector<std::shared_ptr<IFrame>>& frames, bool is_last_batch);

    // Resume frame dispatch after the QPACK decoder has caught up enough to
    // decode the previously-blocked HEADERS frame at the head of
    // pending_blocked_frames_. Returns true once the head HEADERS decoded
    // successfully (which then drains as many follow-on frames as possible),
    // false when it is still blocked. Safe to call repeatedly.
    void DrainPendingFrames();

protected:
    uint64_t header_block_key_{0};
    uint32_t next_section_number_{0};
    std::shared_ptr<QpackEncoder> qpack_encoder_;   // For encoding outgoing headers
    std::shared_ptr<QpackEncoder> qpack_decoder_;   // For decoding incoming headers (RFC 9204 dual-table)
    std::shared_ptr<IQuicBidirectionStream> stream_;
    std::shared_ptr<QpackBlockedRegistry> blocked_registry_;

    // request or response
    std::unordered_map<std::string, std::string> headers_;
    std::shared_ptr<common::IBuffer> body_;
    bool is_last_data_;
    bool current_frame_is_last_;  // Track if current frame is last in OnData batch
    bool should_notify_completion_{false};  // Defer stream completion notification until all frames processed

    bool is_provider_mode_;
    bool all_provider_data_sent_;
    body_provider provider_;

    // Frame decoder for stateful decoding
    FrameDecoder frame_decoder_;

    // Qlog trace for HTTP/3 frame events
    std::shared_ptr<common::QlogTrace> qlog_trace_;

    // ------------------------------------------------------------------
    // QPACK head-of-line blocking buffer.
    //
    // RFC 9204 allows a HEADERS field section to depend on dynamic-table
    // entries that have not yet arrived on the QPACK encoder stream
    // (Required Insert Count > current Insert Count). When that happens
    // the field section is "blocked" and the stream MUST NOT make
    // forward progress past it: a subsequent DATA frame still belongs to
    // the same response and cannot be delivered to the application
    // before its HEADERS have been decoded.
    //
    // Without this buffer, the next frame in the same OnData batch (or a
    // later batch) would call HandleData() against a still-null response_
    // /request_ object, which is exactly the use-after-construct that
    // forced QPACK dynamic table to be disabled by default.
    //
    // pending_blocked_frames_ holds the in-order tail of frames that are
    // waiting for the head HEADERS to unblock; pending_blocked_is_last_
    // remembers the FIN bit of the OnData batch from which those frames
    // came, so we can replay it accurately when we drain.
    // is_currently_blocked_ is the gate: once set, every newly-decoded
    // frame goes straight to the queue rather than to HandleFrame().
    // ------------------------------------------------------------------
    std::deque<std::shared_ptr<IFrame>> pending_blocked_frames_;
    bool pending_blocked_is_last_{false};
    bool is_currently_blocked_{false};
    std::function<void()> blocked_retry_fn_;
};

}  // namespace http3
}  // namespace quicx

#endif
