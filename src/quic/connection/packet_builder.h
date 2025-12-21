#ifndef QUIC_CONNECTION_PACKET_BUILDER
#define QUIC_CONNECTION_PACKET_BUILDER

#include <cstdint>
#include <memory>
#include <vector>

#include "quic/crypto/tls/type.h"
#include "quic/frame/if_frame.h"
#include "quic/packet/if_packet.h"
#include "quic/packet/type.h"

namespace quicx {
namespace quic {

// Forward declarations
class IFrameVisitor;
class ICryptographer;
class ConnectionIDManager;
class PacketNumber;
class StreamManager;
class SendControl;

/**
 * @brief Packet builder for unified packet construction
 *
 * This component unifies the packet building logic that was previously duplicated across:
 * 1. SendManager::MakePacket() - Normal packet building
 * 2. BaseConnection::SendImmediateAckAtLevel() - Immediate ACK packet building
 *
 * Key responsibilities:
 * 1. Create appropriate packet type based on encryption level
 * 2. Set connection IDs (source and destination)
 * 3. Set packet headers (version, packet number length)
 * 4. Configure payload from frame visitor
 * 5. Attach cryptographer for encryption
 * 6. Handle Initial packet requirements (token, padding to 1200 bytes)
 *
 * Usage:
 *   BuildContext ctx;
 *   ctx.encryption_level = kHandshake;
 *   ctx.cryptographer = crypto.GetCryptographer(kHandshake);
 *   ctx.frame_visitor = &visitor;
 *   ctx.local_cid_manager = &local_mgr;
 *   ctx.remote_cid_manager = &remote_mgr;
 *
 *   PacketBuilder builder;
 *   BuildResult result = builder.BuildPacket(ctx);
 *   if (result.success) {
 *       // Use result.packet
 *   }
 */
class PacketBuilder {
public:
    /**
     * @brief Build context containing all input parameters for packet building
     */
    struct BuildContext {
        // Required parameters
        EncryptionLevel encryption_level;                  // Encryption level (Initial, Handshake, 0-RTT, 1-RTT)
        std::shared_ptr<ICryptographer> cryptographer;     // Cryptographer for encryption
        IFrameVisitor* frame_visitor;                      // Frame visitor with payload data
        ConnectionIDManager* local_cid_manager;            // Local connection ID manager
        ConnectionIDManager* remote_cid_manager;           // Remote connection ID manager

        // Optional parameters for Initial packets
        const uint8_t* token_data;                         // Token data (for Initial packets)
        size_t token_length;                               // Token length
        bool add_padding;                                  // Whether to pad Initial packets to 1200 bytes

        // Packet number (if 0, packet number assignment is deferred)
        uint64_t packet_number;                            // Packet number (0 = defer assignment)

        BuildContext()
            : encryption_level(kInitial),
              frame_visitor(nullptr),
              local_cid_manager(nullptr),
              remote_cid_manager(nullptr),
              token_data(nullptr),
              token_length(0),
              add_padding(true),
              packet_number(0) {}
    };

    /**
     * @brief Build result containing the built packet and status
     */
    struct BuildResult {
        bool success;                                      // Whether build succeeded
        std::shared_ptr<IPacket> packet;                   // The built packet (nullptr if failed)
        uint64_t packet_number;                            // Packet number (if success)
        uint32_t packet_size;                              // Packet size in bytes (if success)
        std::string error_message;                         // Error message (if failed)

        BuildResult() : success(false), packet_number(0), packet_size(0) {}
    };

    PacketBuilder() = default;
    ~PacketBuilder() = default;

    /**
     * @brief Build a packet from the given context (low-level interface)
     *
     * This method unifies packet building logic:
     * 1. Creates appropriate packet type based on encryption level
     * 2. Sets connection IDs (source CID for long headers, destination CID for all)
     * 3. Sets version for long headers
     * 4. Attaches payload from frame visitor
     * 5. Attaches cryptographer for encryption
     * 6. For Initial packets: sets token and adds padding if requested
     * 7. If packet_number != 0, sets packet number and packet number length
     *
     * @param ctx Build context with all necessary parameters
     * @return BuildResult with success status and packet (or error message)
     */
    BuildResult BuildPacket(const BuildContext& ctx);

    // ==================== High-level Interfaces ====================

    /**
     * @brief Context for building data packets (frames + optional stream data)
     *
     * This context contains all parameters needed to build a complete data packet,
     * including control frames, ACKs, and stream data.
     */
    struct DataPacketContext {
        // Required: Encryption settings
        EncryptionLevel level;                             // Encryption level
        std::shared_ptr<ICryptographer> cryptographer;     // Cryptographer for encryption

        // Required: Connection ID managers
        ConnectionIDManager* local_cid_manager;            // Local CID manager
        ConnectionIDManager* remote_cid_manager;           // Remote CID manager

        // Optional: Control frames to send
        std::vector<std::shared_ptr<IFrame>> frames;       // Non-stream frames (ACK, PING, etc.)

        // Optional: Stream data
        StreamManager* stream_manager;                     // Stream manager for fetching stream frames
        bool include_stream_data;                          // Whether to include stream data

        // Optional: Initial packet requirements
        std::string token;                                 // Token (for Initial packets)
        bool add_padding;                                  // Whether to pad to 1200 bytes
        uint32_t min_size;                                 // Minimum packet size (for padding)

        DataPacketContext()
            : level(kInitial),
              local_cid_manager(nullptr),
              remote_cid_manager(nullptr),
              stream_manager(nullptr),
              include_stream_data(true),
              add_padding(false),
              min_size(1200) {}
    };

    /**
     * @brief Build a data packet with frames and optional stream data
     *
     * This is the primary packet building interface for normal data transmission.
     * It handles:
     * 1. Adding control frames (ACK, PING, etc.)
     * 2. Fetching and adding stream data (if requested)
     * 3. Padding (for Initial packets)
     * 4. Packet number assignment
     * 5. Encoding to buffer
     * 6. Recording send event for congestion control
     *
     * @param ctx Data packet context with all parameters
     * @param output_buffer Output buffer to write encoded packet
     * @param packet_number Packet number manager (for assigning packet numbers)
     * @param send_control Send control (for recording packet send event)
     * @return BuildResult with success status, packet, packet number, and size
     */
    BuildResult BuildDataPacket(
        const DataPacketContext& ctx,
        std::shared_ptr<common::IBuffer> output_buffer,
        PacketNumber& packet_number,
        SendControl& send_control);

    /**
     * @brief Build a pure ACK packet for immediate sending
     *
     * This is a simplified interface for building ACK-only packets,
     * typically used for cross-level ACKs or immediate ACK sending.
     *
     * @param level Encryption level
     * @param cryptographer Cryptographer for encryption
     * @param ack_frame ACK frame to send
     * @param local_cid_mgr Local connection ID manager
     * @param remote_cid_mgr Remote connection ID manager
     * @param output_buffer Output buffer to write encoded packet
     * @param packet_number Packet number manager
     * @param send_control Send control
     * @return BuildResult with success status and packet info
     */
    BuildResult BuildAckPacket(
        EncryptionLevel level,
        std::shared_ptr<ICryptographer> cryptographer,
        std::shared_ptr<IFrame> ack_frame,
        ConnectionIDManager* local_cid_mgr,
        ConnectionIDManager* remote_cid_mgr,
        std::shared_ptr<common::IBuffer> output_buffer,
        PacketNumber& packet_number,
        SendControl& send_control);

    /**
     * @brief Build a single-frame packet for immediate sending
     *
     * This interface is used for frames that need immediate transmission,
     * such as PATH_CHALLENGE, PATH_RESPONSE, or CONNECTION_CLOSE.
     *
     * @param frame Frame to send
     * @param level Encryption level
     * @param cryptographer Cryptographer for encryption
     * @param local_cid_mgr Local connection ID manager
     * @param remote_cid_mgr Remote connection ID manager
     * @param output_buffer Output buffer to write encoded packet
     * @param packet_number Packet number manager
     * @param send_control Send control
     * @return BuildResult with success status and packet info
     */
    BuildResult BuildImmediatePacket(
        std::shared_ptr<IFrame> frame,
        EncryptionLevel level,
        std::shared_ptr<ICryptographer> cryptographer,
        ConnectionIDManager* local_cid_mgr,
        ConnectionIDManager* remote_cid_mgr,
        std::shared_ptr<common::IBuffer> output_buffer,
        PacketNumber& packet_number,
        SendControl& send_control);

private:
    /**
     * @brief Create packet object based on encryption level
     *
     * @param level Encryption level
     * @return Packet object of appropriate type (InitPacket, HandshakePacket, etc.)
     */
    std::shared_ptr<IPacket> CreatePacketByLevel(EncryptionLevel level);

    /**
     * @brief Set connection IDs on packet header
     *
     * For long headers (Initial, Handshake, 0-RTT):
     * - Sets both source CID and destination CID
     *
     * For short headers (1-RTT):
     * - Sets only destination CID
     *
     * @param packet Packet to configure
     * @param local_cid_manager Local connection ID manager
     * @param remote_cid_manager Remote connection ID manager
     */
    void SetConnectionIDs(
        std::shared_ptr<IPacket> packet, ConnectionIDManager* local_cid_manager, ConnectionIDManager* remote_cid_manager);

    /**
     * @brief Handle Initial packet specific requirements
     *
     * 1. Set token if provided
     * 2. Add padding to reach 1200 bytes minimum (RFC 9000 requirement)
     *
     * @param packet Initial packet to configure
     * @param ctx Build context with token and padding settings
     */
    void HandleInitialPacketRequirements(std::shared_ptr<IPacket> packet, const BuildContext& ctx);
};

}  // namespace quic
}  // namespace quicx

#endif  // QUIC_CONNECTION_PACKET_BUILDER
