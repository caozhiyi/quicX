// Use of this source code is governed by a BSD 3-Clause License
// that can be found in the LICENSE file.

#include <functional>

#include "common/log/log.h"
#include "common/qlog/qlog_manager.h"

namespace quicx {
namespace common {

QlogManager::QlogManager():
    initialized_(false) {
    // defaule config
    config_.enabled = false;
    config_.output_dir = "./qlogs";
    config_.format = QlogFileFormat::kSequential;
    config_.async_queue_size = 10000;
    config_.flush_interval_ms = 100;
    config_.sampling_rate = 1.0f;
}

QlogManager::~QlogManager() {
    // stop writer
    if (writer_) {
        writer_->Stop();
    }

    // clear all Trace
    std::lock_guard<std::mutex> lock(traces_mutex_);
    traces_.clear();
}

void QlogManager::Enable(bool enabled) {
    std::lock_guard<std::mutex> lock(config_mutex_);
    config_.enabled = enabled;

    if (enabled && !initialized_) {
        // init writer
        writer_ = std::make_unique<AsyncWriter>(config_);
        writer_->Start();
        initialized_ = true;

        LOG_INFO("qlog enabled, output_dir=%s", config_.output_dir.c_str());
    } else if (!enabled && initialized_) {
        // stop writer
        writer_->Stop();
        writer_.reset();
        initialized_ = false;

        LOG_INFO("qlog disabled");
    }
}

void QlogManager::SetConfig(const QlogConfig& config) {
    std::lock_guard<std::mutex> lock(config_mutex_);
    bool was_enabled = config_.enabled;
    config_ = config;

    // if state changes, reinitialize
    if (was_enabled != config_.enabled) {
        lock.~lock_guard();  // unlock after calling Enable
        Enable(config_.enabled);
    } else if (config_.enabled && writer_) {
        // update writer config
        writer_->UpdateConfig(config_);
    }
}

void QlogManager::SetOutputDirectory(const std::string& dir) {
    std::lock_guard<std::mutex> lock(config_mutex_);
    config_.output_dir = dir;
    if (writer_) {
        writer_->SetOutputDirectory(dir);
    }
}

void QlogManager::SetEventWhitelist(const std::vector<std::string>& events) {
    std::lock_guard<std::mutex> lock(config_mutex_);
    config_.event_whitelist = events;
}

void QlogManager::SetSamplingRate(float rate) {
    std::lock_guard<std::mutex> lock(config_mutex_);
    if (rate >= 0.0f && rate <= 1.0f) {
        config_.sampling_rate = rate;
    }
}

std::shared_ptr<QlogTrace> QlogManager::CreateTrace(const std::string& connection_id, VantagePoint vantage_point) {
    {
        std::lock_guard<std::mutex> lock(config_mutex_);
        if (!config_.enabled) {
            return nullptr;
        }

        // sample check
        if (!ShouldSampleConnection(connection_id)) {
            return nullptr;
        }
    }

    std::lock_guard<std::mutex> lock(traces_mutex_);

    // check is exists
    auto it = traces_.find(connection_id);
    if (it != traces_.end()) {
        LOG_WARN("qlog trace already exists for connection: %s", connection_id.c_str());
        return it->second;
    }

    // create new Trace
    auto trace = std::make_shared<QlogTrace>(connection_id, vantage_point, config_);
    trace->SetWriter(writer_.get());
    traces_[connection_id] = trace;

    LOG_DEBUG("qlog trace created for connection: %s", connection_id.c_str());
    return trace;
}

void QlogManager::RemoveTrace(const std::string& connection_id) {
    std::lock_guard<std::mutex> lock(traces_mutex_);

    auto it = traces_.find(connection_id);
    if (it != traces_.end()) {
        // flush last event
        it->second->Flush();
        traces_.erase(it);

        LOG_DEBUG("qlog trace removed for connection: %s", connection_id.c_str());
    }
}

std::shared_ptr<QlogTrace> QlogManager::GetTrace(const std::string& connection_id) {
    std::lock_guard<std::mutex> lock(traces_mutex_);

    auto it = traces_.find(connection_id);
    if (it != traces_.end()) {
        return it->second;
    }
    return nullptr;
}

void QlogManager::Flush() {
    if (writer_) {
        writer_->Flush();
    }
}

bool QlogManager::ShouldSampleConnection(const std::string& connection_id) {
    if (config_.sampling_rate >= 1.0f) {
        return true;
    }

    if (config_.sampling_rate <= 0.0f) {
        return false;
    }

    // base on connetion to sample
    std::hash<std::string> hasher;
    size_t hash_value = hasher(connection_id);
    double normalized = (hash_value % 10000) / 10000.0;

    return normalized < config_.sampling_rate;
}

}  // namespace common
}  // namespace quicx
