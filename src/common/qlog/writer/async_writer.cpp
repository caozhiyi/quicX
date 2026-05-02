// Use of this source code is governed by a BSD 3-Clause License
// that can be found in the LICENSE file.

#include "common/qlog/writer/async_writer.h"
#include "common/util/time.h"
#include "common/log/log.h"

#include <filesystem>
#include <algorithm>

namespace quicx {
namespace common {

AsyncWriter::AsyncWriter(const QlogConfig& config)
    : config_(config),
      running_(false),
      total_events_written_(0),
      total_bytes_written_(0) {
}

AsyncWriter::~AsyncWriter() {
    Stop();
}

void AsyncWriter::Start() {
    if (running_.exchange(true)) {
        return;  // already started
    }

    // Create output directory
    try {
        std::filesystem::create_directories(config_.output_dir);
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to create qlog output directory: %s, error: %s",
                  config_.output_dir.c_str(), e.what());
        running_ = false;
        return;
    }

    // Start writer thread
    writer_thread_ = std::thread(&AsyncWriter::WriterLoop, this);

    LOG_INFO("qlog AsyncWriter started, output_dir=%s", config_.output_dir.c_str());
}

void AsyncWriter::Stop() {
    if (!running_.exchange(false)) {
        return;  // already stopped
    }

    // Wait for thread to exit
    if (writer_thread_.joinable()) {
        writer_thread_.join();
    }

    // Close all files
    CloseAllFiles();

    LOG_INFO("qlog AsyncWriter stopped, total_events=%llu, total_bytes=%llu",
             total_events_written_.load(), total_bytes_written_.load());
}

void AsyncWriter::WriteHeader(const std::string& connection_id, const std::string& header) {
    if (!running_.load()) {
        return;
    }

    WriteTask task(connection_id, header, true);
    write_queue_.Push(task);
}

void AsyncWriter::WriteEvent(const std::string& connection_id, const std::string& event) {
    if (!running_.load()) {
        return;
    }

    WriteTask task(connection_id, event, false);
    write_queue_.Push(task);
}

void AsyncWriter::WriterLoop() {
    std::vector<WriteTask> batch;
    batch.reserve(1000);  // pre-allocate

    uint64_t last_flush_time = UTCTimeMsec();

    while (running_.load()) {
        // Batch dequeue tasks
        WriteTask task;
        while (write_queue_.Pop(task)) {
            batch.push_back(std::move(task));

            // Batch size limit
            if (batch.size() >= 1000) {
                break;
            }
        }

        // Time-triggered flush or empty queue
        uint64_t now = UTCTimeMsec();
        bool should_flush = false;

        if (!batch.empty()) {
            if ((now - last_flush_time) >= config_.flush_interval_ms) {
                should_flush = true;
            }

            if (batch.size() >= 1000) {
                should_flush = true;
            }
        }

        if (should_flush) {
            FlushBatch(batch);
            batch.clear();
            last_flush_time = now;
        }

        // Brief sleep when no tasks available
        if (batch.empty() && write_queue_.Empty()) {
            Sleep(10);  // 10ms
        }
    }

    // Flush remaining tasks from the local batch before exit
    if (!batch.empty()) {
        FlushBatch(batch);
        batch.clear();
    }

    // Drain any tasks that were enqueued after the loop exited
    WriteTask remaining_task;
    while (write_queue_.Pop(remaining_task)) {
        batch.push_back(std::move(remaining_task));
    }
    if (!batch.empty()) {
        FlushBatch(batch);
    }
}

void AsyncWriter::FlushBatch(std::vector<WriteTask>& batch) {
    std::lock_guard<std::mutex> lock(files_mutex_);

    for (const auto& task : batch) {
        try {
            auto& file = GetOrCreateFile(task.connection_id);

            if (file.is_open()) {
                file << task.data;

                if (!task.is_header) {
                    total_events_written_++;
                }
                total_bytes_written_ += task.data.size();
            } else {
                LOG_ERROR("Failed to write qlog for connection: %s", task.connection_id.c_str());
            }
        } catch (const std::exception& e) {
            LOG_ERROR("Exception while writing qlog: %s", e.what());
        }
    }

    // Flush file buffers
    if (config_.batch_write) {
        for (auto& pair : file_streams_) {
            if (pair.second && pair.second->is_open()) {
                pair.second->flush();
            }
        }
    }
}

std::ofstream& AsyncWriter::GetOrCreateFile(const std::string& connection_id) {
    // File already exists
    auto it = file_streams_.find(connection_id);
    if (it != file_streams_.end() && it->second && it->second->is_open()) {
        return *(it->second);
    }

    // Create new file
    std::string filename = GenerateFilename(connection_id);
    auto file = std::make_unique<std::ofstream>(filename,
                                                std::ios::out | std::ios::trunc);

    if (!file->is_open()) {
        LOG_ERROR("Failed to open qlog file: %s", filename.c_str());
    } else {
        LOG_DEBUG("Created qlog file: %s", filename.c_str());
    }

    file_streams_[connection_id] = std::move(file);
    return *(file_streams_[connection_id]);
}

void AsyncWriter::CloseFile(const std::string& connection_id) {
    std::lock_guard<std::mutex> lock(files_mutex_);

    auto it = file_streams_.find(connection_id);
    if (it != file_streams_.end()) {
        if (it->second && it->second->is_open()) {
            it->second->flush();
            it->second->close();
        }
        file_streams_.erase(it);
    }
}

void AsyncWriter::CloseAllFiles() {
    std::lock_guard<std::mutex> lock(files_mutex_);

    for (auto& pair : file_streams_) {
        if (pair.second && pair.second->is_open()) {
            pair.second->flush();
            pair.second->close();
        }
    }
    file_streams_.clear();
}

std::string AsyncWriter::GenerateFilename(const std::string& connection_id) {
    // Format: {output_dir}/{timestamp}_{cid_prefix}.qlog
    std::string timestamp = GetFormatTime(FormatTimeUnit::kSecondFormat);

    // Replace ':' with '-' in timestamp (filenames cannot contain ':')
    std::replace(timestamp.begin(), timestamp.end(), ':', '-');

    // Connection ID prefix (up to 8 characters)
    std::string cid_prefix = connection_id.substr(0, std::min<size_t>(8, connection_id.size()));

    std::string filename = config_.output_dir + "/" + timestamp + "_" + cid_prefix + ".qlog";
    return filename;
}

void AsyncWriter::Flush() {
    std::lock_guard<std::mutex> lock(files_mutex_);

    for (auto& pair : file_streams_) {
        if (pair.second && pair.second->is_open()) {
            pair.second->flush();
        }
    }
}

void AsyncWriter::UpdateConfig(const QlogConfig& config) {
    std::lock_guard<std::mutex> lock(config_mutex_);
    config_ = config;
}

void AsyncWriter::SetOutputDirectory(const std::string& dir) {
    std::lock_guard<std::mutex> lock(config_mutex_);
    config_.output_dir = dir;

    // Create new directory
    try {
        std::filesystem::create_directories(dir);
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to create directory: %s, error: %s", dir.c_str(), e.what());
    }
}

}  // namespace common
}  // namespace quicx
