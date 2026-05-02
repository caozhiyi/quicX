// Use of this source code is governed by a BSD 3-Clause License
// that can be found in the LICENSE file.

#ifndef COMMON_QLOG_WRITER_ASYNC_WRITER
#define COMMON_QLOG_WRITER_ASYNC_WRITER

#include <thread>
#include <atomic>
#include <map>
#include <fstream>
#include <memory>
#include <mutex>

#include "common/qlog/qlog_config.h"
#include "common/structure/thread_safe_queue.h"

namespace quicx {
namespace common {

/**
 * @brief Async write task
 */
struct WriteTask {
    std::string connection_id;
    std::string data;
    bool is_header = false;  // true: header; false: event

    WriteTask() = default;
    WriteTask(const std::string& cid, const std::string& d, bool header = false)
        : connection_id(cid), data(d), is_header(header) {}
};

/**
 * @brief Async qlog writer
 *
 * Responsibilities:
 * - Maintain writer thread
 * - Manage per-connection file handles
 * - Batch write optimization
 * - File flush control
 */
class AsyncWriter {
public:
    explicit AsyncWriter(const QlogConfig& config);
    ~AsyncWriter();

    // Start/Stop
    void Start();
    void Stop();

    // Write interface
    void WriteHeader(const std::string& connection_id, const std::string& header);
    void WriteEvent(const std::string& connection_id, const std::string& event);

    // Flush
    void Flush();

    // Configuration update
    void UpdateConfig(const QlogConfig& config);
    void SetOutputDirectory(const std::string& dir);

    // Statistics
    uint64_t GetTotalEventsWritten() const { return total_events_written_.load(); }
    uint64_t GetTotalBytesWritten() const { return total_bytes_written_.load(); }

private:
    // Writer thread main loop
    void WriterLoop();

    // Batch flush
    void FlushBatch(std::vector<WriteTask>& batch);

    // File management
    std::ofstream& GetOrCreateFile(const std::string& connection_id);
    void CloseFile(const std::string& connection_id);
    void CloseAllFiles();
    std::string GenerateFilename(const std::string& connection_id);

    // Configuration
    QlogConfig config_;
    std::mutex config_mutex_;

    // Write queue
    ThreadSafeQueue<WriteTask> write_queue_;

    // Writer thread
    std::thread writer_thread_;
    std::atomic<bool> running_;

    // File handle map (connection_id -> ofstream)
    std::map<std::string, std::unique_ptr<std::ofstream>> file_streams_;
    std::mutex files_mutex_;

    // Statistics
    std::atomic<uint64_t> total_events_written_;
    std::atomic<uint64_t> total_bytes_written_;
};

}  // namespace common
}  // namespace quicx

#endif
