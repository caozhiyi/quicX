#ifndef COMMON_QLOG_QLOG_TRACE
#define COMMON_QLOG_QLOG_TRACE

#include <atomic>
#include <memory>
#include <string>
#include <thread>

#include "common/qlog/event/connectivity_events.h"
#include "common/qlog/event/qlog_event.h"
#include "common/qlog/event/recovery_events.h"
#include "common/qlog/event/transport_events.h"
#include "common/qlog/qlog_config.h"
#include "common/qlog/serializer/if_qlog_serializer.h"

namespace quicx {
namespace common {

class AsyncWriter;

/**
 * @brief qlog trace for a single connection
 *
 * Responsibilities:
 * - Record all events for the connection
 * - Manage common_fields
 * - Event serialization
 */
class QlogTrace {
public:
    QlogTrace(const std::string& connection_id, VantagePoint vantage_point, const QlogConfig& config);
    ~QlogTrace();

    // ========== Event logging interface ==========

    /**
     * @brief Log generic event
     *
     * @param time_us Microsecond timestamp (absolute time)
     * @param event_name Event name (e.g. "quic:packet_sent")
     * @param data Event data (ownership transferred)
     */
    void LogEvent(uint64_t time_us, const std::string& event_name, std::unique_ptr<EventData> data);

    // ========== Convenience methods (high-frequency events) ==========

    /**
     * @brief Log packet_sent event
     */
    void LogPacketSent(uint64_t time_us, const PacketSentData& data);

    /**
     * @brief Log packet_received event
     */
    void LogPacketReceived(uint64_t time_us, const PacketReceivedData& data);

    /**
     * @brief Log recovery_metrics_updated event
     */
    void LogMetricsUpdated(uint64_t time_us, const RecoveryMetricsData& data);

    /**
     * @brief Log connection_started event
     */
    void LogConnectionStarted(uint64_t time_us, const ConnectionStartedData& data);

    /**
     * @brief Log connection_closed event
     */
    void LogConnectionClosed(uint64_t time_us, const ConnectionClosedData& data);

    // ========== Metadata ==========

    /**
     * @brief Set common fields (reduce repetition)
     */
    void SetCommonFields(const CommonFields& fields);

    /**
     * @brief Set configuration info
     */
    void SetConfiguration(const QlogConfiguration& config);

    // ========== Write control ==========

    /**
     * @brief Flush buffered events
     */
    void Flush();

    /**
     * @brief Set writer (called by QlogManager)
     */
    void SetWriter(AsyncWriter* writer) { writer_ = writer; }

    // ========== Accessors ==========

    const std::string& GetConnectionId() const { return connection_id_; }
    VantagePoint GetVantagePoint() const { return vantage_point_; }
    uint64_t GetEventCount() const { return event_count_.load(); }

private:
    // Check if event should be logged (filter)
    bool ShouldLogEvent(const std::string& event_name);

    // Serialize and write event
    void WriteEvent(const QlogEvent& event);

    // Basic info
    std::string connection_id_;
    VantagePoint vantage_point_;

    // Configuration
    QlogConfig config_;

    // Metadata
    CommonFields common_fields_;
    QlogConfiguration configuration_;

    // Time management
    uint64_t start_time_us_;  // Trace start time
    bool use_relative_time_;  // Whether to use relative time

    // Event statistics
    std::atomic<uint64_t> event_count_;

    // Serializer
    std::unique_ptr<IQlogSerializer> serializer_;

    // Writer
    AsyncWriter* writer_;

    // Thread safety: Connection is bound to a single thread, no mutex needed.
    // NOTE: `owner_thread_id_` is intentionally ALWAYS present (not guarded by
    // NDEBUG). An earlier version guarded it with `#ifndef NDEBUG`, which made
    // sizeof(QlogTrace) differ between translation units compiled with and
    // without NDEBUG (e.g. the benchmark forced NDEBUG while the library did
    // not). That size mismatch meant the allocator (smaller layout) and the
    // constructor (larger layout) disagreed, producing a 1-byte
    // heap-buffer-overflow that corrupted the next allocation's vtable pointer
    // and crashed inside PacketSentData::ToJson(). Keeping the member
    // unconditional makes the layout stable regardless of build flags.
    std::thread::id owner_thread_id_;

    // Whether header has been written
    bool header_written_;
};

}  // namespace common
}  // namespace quicx

#endif  // COMMON_QLOG_QLOG_TRACE
