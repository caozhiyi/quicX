#ifndef HTTP3_STREAM_PUSH_RECEIVER_STREAM
#define HTTP3_STREAM_PUSH_RECEIVER_STREAM

#include <memory>
#include <quicx/http3/type.h>
#include <quicx/quic/if_quic_recv_stream.h>
#include <string>
#include <unordered_map>

#include "http3/frame/frame_decoder.h"
#include "http3/frame/if_frame.h"
#include "http3/qpack/qpack_decoder.h"
#include "http3/qpack/qpack_encoder.h"
#include "http3/stream/if_recv_stream.h"

namespace quicx {
namespace http3 {

/**
 * @brief Push receiver stream
 *
 * The push receiver stream is used to receive push frames from the server.
 */
class PushReceiverStream: public IRecvStream {
public:
    // |push_id_validator| (P1-H6f, RFC 9114 §4.6): invoked once with the
    // stream's push id right after the varint is parsed. Returns false when
    // the id exceeds the advertised MAX_PUSH_ID or was already consumed by
    // another push stream — both are H3_ID_ERROR conditions. May be null in
    // tests (validation skipped).
    PushReceiverStream(const std::shared_ptr<QpackDecoder>& qpack_decoder,
        const std::shared_ptr<IQuicRecvStream>& stream,
        const std::function<void(uint64_t stream_id, uint32_t error_code)>& error_handler,
        const http_response_handler& response_handler,
        const std::function<bool(uint64_t)>& push_id_validator = nullptr);
    virtual ~PushReceiverStream();

    virtual void OnData(std::shared_ptr<IBufferRead> data, bool is_last, uint32_t error) override;

private:
    virtual void HandleHeaders(std::shared_ptr<IFrame> frame);
    virtual void HandleData(std::shared_ptr<IFrame> frame);
    virtual void HandleBody();

private:
    enum class ParseState {
        kReadingPushId,  // Reading push ID (stream type already read by UnidentifiedStream)
        kReadingFrames   // Reading HTTP3 frames (HEADERS + DATA)
    };

    std::shared_ptr<QpackDecoder> qpack_decoder_;  // RFC 9204: use decoder table for incoming headers

    http_response_handler response_handler_;

    // P1-H6f: RFC 9114 §4.6 push-id validation against the connection's
    // MAX_PUSH_ID window and per-connection consumption set.
    std::function<bool(uint64_t)> push_id_validator_;

    ParseState parse_state_;
    uint64_t push_id_;

    uint32_t body_length_;
    std::shared_ptr<common::IBuffer> body_;
    HttpFields headers_;  // ordered field lines (RFC 9110 §5.3)

    // Frame decoder for stateful decoding
    FrameDecoder frame_decoder_;
};

}  // namespace http3
}  // namespace quicx

#endif  // HTTP3_STREAM_PUSH_RECEIVER_STREAM
