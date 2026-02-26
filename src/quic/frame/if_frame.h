#ifndef QUIC_FRAME_IF_FRAME
#define QUIC_FRAME_IF_FRAME

#include <memory>
#include "quic/frame/type.h"
#include "common/buffer/if_buffer.h"

namespace quicx {
namespace quic {

/**
 * @brief Base interface for QUIC frames
 *
 * All QUIC frame types implement this interface for encoding/decoding operations.
 */
class IFrame {
public:
    IFrame(uint16_t ft = FrameType::kUnknown);
    virtual ~IFrame();

    /**
     * @brief Get the frame type
     *
     * @return Frame type identifier
     */
    uint16_t GetType();

    /**
     * @brief Encode the frame into a buffer
     *
     * @param buffer Destination buffer
     * @return true if encoded successfully, false otherwise
     */
    virtual bool Encode(std::shared_ptr<common::IBuffer> buffer);

    /**
     * @brief Decode the frame from a buffer
     *
     * @param buffer Source buffer
     * @param with_type Whether the buffer includes the frame type byte
     * @return true if decoded successfully, false otherwise
     */
    virtual bool Decode(std::shared_ptr<common::IBuffer> buffer, bool with_type = false);

    /**
     * @brief Calculate the encoded size of this frame
     *
     * @return Number of bytes required to encode
     */
    virtual uint32_t EncodeSize();

    /**
     * @brief Get the frame type bit representation
     *
     * @return Frame type bits
     */
    virtual uint32_t GetFrameTypeBit();

protected:
    uint16_t frame_type_;
};

}
}

#endif