#ifndef QUIC_FRAME_IF_FRAME
#define QUIC_FRAME_IF_FRAME

#include <functional>
#include <memory>

#include "common/buffer/if_buffer.h"

#include "quic/frame/type.h"

namespace quicx {
namespace quic {

/**
 * @brief Frame-level delivery outcome, fired when the fate of the packet
 *        carrying the frame is decided (aioquic QuicDeliveryState model).
 *
 * RFC 9000 acknowledges *packets*, not frames; a frame rides in a packet, so
 * its delivery is inferred from that packet's fate. RFC 9002 drives the two
 * transitions: ACK of the packet number => kAcked; threshold/PTO loss of the
 * packet number => kLost.
 */
enum class FrameDeliveryState {
    kAcked,  ///< the packet carrying this frame was acknowledged
    kLost,   ///< the packet carrying this frame was declared lost (or PTO-probed away)
};

/**
 * @brief Delivery tracking callback attached to a frame before it is queued.
 *
 * Handlers MUST be idempotent "state reset" closures (aioquic's
 * _on_*_delivery pattern): on kLost they flip the producer's state so the
 * regular send loop re-emits a *fresh* frame carrying current values, rather
 * than re-queuing this frame object. They may fire more than once (a packet
 * can be declared lost again after a PTO re-queue), which the reset style
 * absorbs naturally.
 *
 * Only attach handlers to frames that travel in ACK-eliciting packets:
 * ACK-only / PADDING-only packets are never tracked in the unacked table,
 * so a handler on such a frame would never fire.
 */
using FrameDeliveryHandler = std::function<void(FrameDeliveryState)>;

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

    /**
     * @name Frame-level delivery tracking
     *
     * Optional per-frame callback fired by SendControl when the packet that
     * carried this frame is acknowledged or declared lost. See
     * FrameDeliveryHandler for the contract.
     */
    ///@{
    void SetDeliveryHandler(FrameDeliveryHandler handler) { delivery_handler_ = std::move(handler); }
    bool HasDeliveryHandler() const { return delivery_handler_ != nullptr; }

    // Not fire-and-forget: the handler stays attached so re-queued copies of
    // the same packet (PTO re-queue reuses the IPacket in place) keep their
    // tracking. Reset-style handlers make repeat fires harmless.
    void NotifyDelivery(FrameDeliveryState state) {
        if (delivery_handler_) {
            delivery_handler_(state);
        }
    }
    ///@}

protected:
    uint16_t frame_type_;

    // Delivery tracking callback; null on the overwhelming majority of frames
    // (only control frames needing loss-response carry one).
    FrameDeliveryHandler delivery_handler_;
};

}  // namespace quic
}  // namespace quicx

#endif  // QUIC_FRAME_IF_FRAME