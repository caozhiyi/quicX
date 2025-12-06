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
 * @brief 异步写入任务
 */
struct WriteTask {
    std::string connection_id;
    std::string data;
    bool is_header = false;  // true: 头部; false: 事件

    WriteTask() = default;
    WriteTask(const std::string& cid, const std::string& d, bool header = false)
        : connection_id(cid), data(d), is_header(header) {}
};

/**
 * @brief 异步 qlog 写入器
 *
 * 职责：
 * - 维护写入线程
 * - 管理每个连接的文件句柄
 * - 批量写入优化
 * - 文件刷新控制
 */
class AsyncWriter {
public:
    explicit AsyncWriter(const QlogConfig& config);
    ~AsyncWriter();

    // 启动/停止
    void Start();
    void Stop();

    // 写入接口
    void WriteHeader(const std::string& connection_id, const std::string& header);
    void WriteEvent(const std::string& connection_id, const std::string& event);

    // 刷新
    void Flush();

    // 配置更新
    void UpdateConfig(const QlogConfig& config);
    void SetOutputDirectory(const std::string& dir);

    // 统计信息
    uint64_t GetTotalEventsWritten() const { return total_events_written_.load(); }
    uint64_t GetTotalBytesWritten() const { return total_bytes_written_.load(); }

private:
    // 写入线程主循环
    void WriterLoop();

    // 批量刷新
    void FlushBatch(std::vector<WriteTask>& batch);

    // 文件管理
    std::ofstream& GetOrCreateFile(const std::string& connection_id);
    void CloseFile(const std::string& connection_id);
    void CloseAllFiles();
    std::string GenerateFilename(const std::string& connection_id);

    // 配置
    QlogConfig config_;
    std::mutex config_mutex_;

    // 写入队列
    ThreadSafeQueue<WriteTask> write_queue_;

    // 写入线程
    std::thread writer_thread_;
    std::atomic<bool> running_;

    // 文件句柄映射 (connection_id -> ofstream)
    std::map<std::string, std::unique_ptr<std::ofstream>> file_streams_;
    std::mutex files_mutex_;

    // 统计
    std::atomic<uint64_t> total_events_written_;
    std::atomic<uint64_t> total_bytes_written_;
};

}  // namespace common
}  // namespace quicx

#endif
