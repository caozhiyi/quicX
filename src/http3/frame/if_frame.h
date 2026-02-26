#ifndef HTTP3_FRAME_IF_FRAME
#define HTTP3_FRAME_IF_FRAME

#include <memory>
#include <cstdint>
#include "http3/frame/type.h"
#include "common/buffer/if_buffer.h"

namespace quicx {
namespace http3 {

/**
 * @brief Base interface for HTTP/3 frames
 *
 * All HTTP/3 frame types implement this interface.
 */
class IFrame {
public:
    IFrame(FrameType ft = FrameType::kUnknown): type_(ft) {}
    virtual ~IFrame() {}

    /**
     * @brief Get the frame type
     *
     * @return Frame type identifier
     */
    uint16_t GetType() { return type_; }

    /**
     * @brief Encode the frame into a buffer
     *
     * @param buffer Destination buffer
     * @return true if encoded successfully, false otherwise
     */
    virtual bool Encode(std::shared_ptr<common::IBuffer> buffer) = 0;

    /**
     * @brief Decode the frame from a buffer
     *
     * @param buffer Source buffer
     * @param with_type Whether buffer includes frame type byte
     * @return Decode result status
     */
    virtual DecodeResult Decode(std::shared_ptr<common::IBuffer> buffer, bool with_type = false) = 0;

    /**
     * @brief Calculate encoded size of this frame
     *
     * @return Number of bytes required to encode
     */
    virtual uint32_t EvaluateEncodeSize() = 0;

    /**
     * @brief Calculate payload size
     *
     * @return Payload size in bytes
     */
    virtual uint32_t EvaluatePayloadSize() = 0;

protected:
    uint16_t type_;
};

}  // namespace http3
}  // namespace quicx

#endif
