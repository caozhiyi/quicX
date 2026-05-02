#ifndef QUIC_PACKET_PACKET_INTERFACE
#define QUIC_PACKET_PACKET_INTERFACE

#include <cstdint>
#include <memory>
#include <vector>

#include "common/buffer/if_buffer.h"
#include "common/buffer/shared_buffer_span.h"

#include "quic/frame/type.h"
#include "quic/frame/if_frame.h"
#include "quic/packet/header/if_header.h"
#include "quic/crypto/if_cryptographer.h"

namespace quicx {
namespace quic {

/**
 * @brief Interface for QUIC packets
 *
 * Represents a complete QUIC packet with header, frames, and encryption.
 */
class IPacket {
public:
    IPacket(): frame_type_bit_(0), packet_number_(0), largest_received_pn_(0) {}
    virtual ~IPacket() {}

    /**
     * @brief Get the encryption level of this packet
     *
     * @return Crypto level identifier
     */
    virtual uint16_t GetCryptoLevel() const = 0;

    /**
     * @brief Encode the packet into a buffer
     *
     * @param buffer Destination buffer
     * @return true if encoded successfully, false otherwise
     */
    virtual bool Encode(std::shared_ptr<common::IBuffer> buffer) = 0;

    /**
     * @brief Decode packet header without decrypting payload
     *
     * @param buffer Source buffer
     * @param with_flag Whether buffer includes packet flags
     * @return true if decoded successfully, false otherwise
     */
    virtual bool DecodeWithoutCrypto(std::shared_ptr<common::IBuffer> buffer, bool with_flag = false) = 0;

    /**
     * @brief Decode and decrypt the packet
     *
     * @param buffer Source buffer
     * @return true if decoded successfully, false otherwise
     */
    virtual bool DecodeWithCrypto(std::shared_ptr<common::IBuffer> buffer) = 0;

    /**
     * @brief Get the packet header
     *
     * @return Header instance
     */
    virtual IHeader* GetHeader() = 0;

    /**
     * @brief Get all frames in this packet
     *
     * @return Vector of frame pointers
     */
    virtual std::vector<std::shared_ptr<IFrame>>& GetFrames();

    /**
     * @brief Get packet number offset in encoded form
     *
     * @return Byte offset of packet number
     */
    uint32_t GetPacketNumOffset() { return 0; };

    /**
     * @brief Get the packet number
     *
     * @return Packet number
     */
    uint64_t GetPacketNumber() { return packet_number_; }

    /**
     * @brief Set the packet number
     *
     * @param num Packet number
     */
    void SetPacketNumber(uint64_t num) { packet_number_ = num; }

    /**
     * @brief Get the source buffer span
     *
     * @return Buffer span containing raw packet data
     */
    common::SharedBufferSpan& GetSrcBuffer() { return packet_src_data_; }

    /**
     * @brief Add a frame type bit flag
     *
     * @param bit Frame type bit to add
     */
    void AddFrameTypeBit(FrameTypeBit bit) { frame_type_bit_ |= bit; }

    /**
     * @brief Get combined frame type bits
     *
     * @return Bitwise OR of all frame types
     */
    uint32_t GetFrameTypeBit() { return frame_type_bit_; }

    /**
     * @brief Set packet payload
     *
     * @param payload Payload buffer span
     */
    virtual void SetPayload(const common::SharedBufferSpan& payload) {}

    /**
     * @brief Set the largest received packet number for PN recovery
     *
     * RFC 9000 Appendix A: The full packet number is recovered from the
     * truncated encoding using the largest successfully received packet number.
     *
     * @param largest_pn Largest packet number successfully received so far
     */
    void SetLargestReceivedPn(uint64_t largest_pn) { largest_received_pn_ = largest_pn; }

    /**
     * @brief Get the largest received packet number
     *
     * @return Largest PN set via SetLargestReceivedPn
     */
    uint64_t GetLargestReceivedPn() const { return largest_received_pn_; }

    /**
     * @brief Set the cryptographer for encryption/decryption
     *
     * @param crypto_grapher Cryptographer instance
     */
    void SetCryptographer(std::shared_ptr<ICryptographer> crypto_grapher) { crypto_grapher_ = crypto_grapher; }

    /**
     * @brief RFC 9001 §6: Key Phase tracking for Key Update
     */
    void SetKeyPhaseChanged(bool changed) { key_phase_changed_ = changed; }
    bool IsKeyPhaseChanged() const { return key_phase_changed_; }

protected:
    uint32_t frame_type_bit_;
    uint64_t packet_number_;
    uint64_t largest_received_pn_;  // RFC 9000 Appendix A: for PN recovery
    bool key_phase_changed_ = false;  // RFC 9001 §6: set when key phase differs from expected
    common::SharedBufferSpan packet_src_data_;

    std::shared_ptr<ICryptographer> crypto_grapher_;
};

}
}

#endif