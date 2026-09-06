#ifndef COMMON_QLOG_QLOG_MANAGER
#define COMMON_QLOG_QLOG_MANAGER

#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <string>

#include "common/qlog/qlog_config.h"
#include "common/qlog/qlog_trace.h"
#include "common/qlog/writer/async_writer.h"
#include "common/util/singleton.h"

namespace quicx {
namespace common {

/**
 * @brief qlog global manager (singleton pattern)
 *
 * Responsibilities:
 * - Manage QlogTrace instances for all connections
 * - Global configuration management
 * - Async write thread management
 */
class QlogManager: public Singleton<QlogManager> {
public:
    QlogManager();
    ~QlogManager();

    // ========== Configuration interface ==========

    /**
     * @brief Enable or disable qlog
     */
    void Enable(bool enabled);

    /**
     * @brief Check if qlog is enabled
     *
     * Uses a dedicated atomic so the (hot, per-event) check does not take the
     * config mutex and cannot race with Enable()/SetConfig() updating
     * config_.enabled_ on another thread (TSan data race).
     */
    bool IsEnabled() const { return enabled_.load(std::memory_order_acquire); }

    /**
     * @brief Set complete configuration
     */
    void SetConfig(const QlogConfig& config);

    /**
     * @brief Get current configuration
     */
    const QlogConfig& GetConfig() const { return config_; }

    /**
     * @brief Set output directory
     */
    void SetOutputDirectory(const std::string& dir);

    /**
     * @brief Set event filter (whitelist)
     */
    void SetEventWhitelist(const std::vector<std::string>& events);

    /**
     * @brief Set sampling rate (0.0 - 1.0)
     */
    void SetSamplingRate(float rate);

    // ========== Trace management ==========

    /**
     * @brief Create Trace for new connection
     *
     * @param connection_id Connection ID (hexadecimal string)
     * @param vantage_point Vantage point (client/server)
     * @return Trace smart pointer (nullptr on failure)
     */
    std::shared_ptr<QlogTrace> CreateTrace(const std::string& connection_id, VantagePoint vantage_point);

    /**
     * @brief Remove connection's Trace (called when connection closes)
     *
     * @param connection_id Connection ID
     */
    void RemoveTrace(const std::string& connection_id);

    /**
     * @brief Get existing Trace (for unit testing)
     */
    std::shared_ptr<QlogTrace> GetTrace(const std::string& connection_id);

    // ========== Write control ==========

    /**
     * @brief Flush all buffered events to file
     */
    void Flush();

    /**
     * @brief Get writer (internal use)
     */
    AsyncWriter* GetWriter() { return writer_.get(); }

private:
    // Check if connection should be sampled
    bool ShouldSampleConnection(const std::string& connection_id);

    // Configuration
    QlogConfig config_;
    mutable std::mutex config_mutex_;

    // Trace map (connection_id -> QlogTrace)
    std::map<std::string, std::shared_ptr<QlogTrace>> traces_;
    std::mutex traces_mutex_;

    // Async writer
    std::unique_ptr<AsyncWriter> writer_;

    // Whether initialized
    bool initialized_;

    // Mirror of config_.enabled_ for lock-free, race-free reads in IsEnabled().
    std::atomic<bool> enabled_{false};
};

}  // namespace common
}  // namespace quicx

#endif  // COMMON_QLOG_QLOG_MANAGER
