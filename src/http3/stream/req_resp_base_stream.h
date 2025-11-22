#ifndef HTTP3_STREAM_REQ_RESP_BASE_STREAM
#define HTTP3_STREAM_REQ_RESP_BASE_STREAM

#include <memory>
#include <unordered_map>

#include "http3/include/type.h"
#include "http3/frame/if_frame.h"
#include "http3/frame/frame_decoder.h"
#include "http3/stream/if_stream.h"
#include "http3/qpack/qpack_encoder.h"
#include "http3/qpack/blocked_registry.h"
#include "quic/include/if_quic_bidirection_stream.h"

namespace quicx {
namespace http3 {

/**
 * @brief ReqRespBaseStream is the base class for all request and response
 * streams
 *
 * The req resp base stream is used to handle the request and response frames.
 * It is responsible for handling the headers and data frames.
 */
class ReqRespBaseStream: public IStream {
public:
    ReqRespBaseStream(const std::shared_ptr<QpackEncoder>& qpack_encoder,
        const std::shared_ptr<QpackBlockedRegistry>& blocked_registry,
        const std::shared_ptr<IQuicBidirectionStream>& stream,
        const std::function<void(uint64_t stream_id, uint32_t error_code)>& error_handler);
    virtual ~ReqRespBaseStream();

    virtual uint64_t GetStreamID() override { return stream_->GetStreamID(); }
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

protected:
    uint64_t header_block_key_{0};
    uint32_t next_section_number_{0};
    std::shared_ptr<QpackEncoder> qpack_encoder_;
    std::shared_ptr<IQuicBidirectionStream> stream_;
    std::shared_ptr<QpackBlockedRegistry> blocked_registry_;

    // request or response
    std::unordered_map<std::string, std::string> headers_;
    std::shared_ptr<common::IBuffer> body_;
    bool is_last_data_;

    bool is_provider_mode_;
    body_provider provider_;

    // Frame decoder for stateful decoding
    FrameDecoder frame_decoder_;
};

}  // namespace http3
}  // namespace quicx

#endif
