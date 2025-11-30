#ifndef HTTP3_CONNECTION_IF_CONNECTION
#define HTTP3_CONNECTION_IF_CONNECTION

#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>

#include "http3/include/type.h"
#include "http3/qpack/blocked_registry.h"
#include "http3/qpack/qpack_encoder.h"
#include "http3/stream/if_stream.h"
#include "quic/include/if_quic_connection.h"

namespace quicx {
namespace http3 {

/**
 * @brief IConnection is the base class for all HTTP/3 connections
 *
 * This class is used to manage the HTTP/3 connection.
 */
class IConnection {
public:
    /**
     * @brief Constructor
     * @param unique_id The unique id of the connection
     * @param quic_connection The QUIC connection
     * @param error_handler The error handler
     */
    IConnection(const std::string& unique_id, const std::shared_ptr<IQuicConnection>& quic_connection,
        const std::function<void(const std::string& unique_id, uint32_t error_code)>& error_handler);
    virtual ~IConnection();

    /**
     * @brief Get the unique id of the connection
     * @return The unique id of the connection
     */
    const std::string& GetUniqueId() const { return unique_id_; }

    /**
     * @brief Close the connection
     * @param error_code The error code
     */
    virtual void Close(uint32_t error_code);

protected:
    // handle stream
    virtual void HandleStream(std::shared_ptr<IQuicStream> stream, uint32_t error_code) = 0;
    // handle error
    virtual void HandleError(uint64_t stream_id, uint32_t error_code) = 0;
    // handle settings
    virtual void HandleSettings(const std::unordered_map<uint16_t, uint64_t>& settings);

    static const std::unordered_map<uint16_t, uint64_t> AdaptSettings(const Http3Settings& settings);

    /**
     * @brief Check if peer SETTINGS has been received
     * @return True if SETTINGS received, false otherwise
     */
    bool SettingsReceived() const { return settings_received_; }

protected:
    // Schedule stream removal - moves stream to holding area to delay destruction
    void ScheduleStreamRemoval(uint64_t stream_id);

private:
    // Start periodic cleanup timer for destroyed streams
    void StartCleanupTimer();
    // Clean up destroyed streams (called by timer)
    void CleanupDestroyedStreams();

protected:
    // indicate the unique id of the connection
    std::string unique_id_;
    std::shared_ptr<QpackBlockedRegistry> blocked_registry_;
    std::function<void(const std::string& unique_id, uint32_t error_code)> error_handler_;
    std::unordered_map<uint16_t, uint64_t> settings_;
    std::unordered_map<uint64_t, std::shared_ptr<IStream>> streams_;

    std::shared_ptr<QpackEncoder> qpack_encoder_;

    std::shared_ptr<IQuicConnection> quic_connection_;

    // RFC 9114 Section 4.1: Track if peer SETTINGS frame has been received
    bool settings_received_ = false;

    // Temporary holding area for completed streams to delay destruction
    // This prevents use-after-free when error_handler_ is called from within stream callbacks
    std::vector<std::shared_ptr<IStream>> streams_to_destroy_;

    // Timer ID for periodic cleanup of destroyed streams
    uint64_t cleanup_timer_id_;
};

}  // namespace http3
}  // namespace quicx

#endif
