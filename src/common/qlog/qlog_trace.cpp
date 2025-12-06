// Use of this source code is governed by a BSD 3-Clause License
// that can be found in the LICENSE file.

#include <algorithm>

#include "common/log/log.h"
#include "common/qlog/qlog_trace.h"
#include "common/qlog/serializer/json_seq_serializer.h"
#include "common/qlog/util/qlog_constants.h"
#include "common/qlog/writer/async_writer.h"
#include "common/util/time.h"

namespace quicx {
namespace common {

QlogTrace::QlogTrace(const std::string& connection_id, VantagePoint vantage_point, const QlogConfig& config):
    connection_id_(connection_id),
    vantage_point_(vantage_point),
    config_(config),
    start_time_us_(UTCTimeMsec() * 1000),  // trans to ms
    use_relative_time_(config.time_format == "relative"),
    event_count_(0),
    writer_(nullptr),
#ifndef NDEBUG
    owner_thread_id_(std::this_thread::get_id()),
#endif
    header_written_(false) {
    // create serializer
    if (config_.format == QlogFileFormat::kSequential) {
        serializer_ = std::make_unique<JsonSeqSerializer>();
    } else {
        // future can extend other format
        serializer_ = std::make_unique<JsonSeqSerializer>();
    }
}

QlogTrace::~QlogTrace() {
    Flush();
}

void QlogTrace::LogEvent(uint64_t time_us, const std::string& event_name, std::unique_ptr<EventData> data) {
    // quick check
    if (!writer_ || !ShouldLogEvent(event_name)) {
        return;
    }

    // construct event
    QlogEvent event;
    event.time_us = use_relative_time_ ? (time_us - start_time_us_) : time_us;
    event.name = event_name;
    event.data = std::move(data);

    WriteEvent(event);
    event_count_++;
}

void QlogTrace::LogPacketSent(uint64_t time_us, const PacketSentData& data) {
    auto event_data = std::make_unique<PacketSentData>(data);
    LogEvent(time_us, QlogEvents::kPacketSent, std::move(event_data));
}

void QlogTrace::LogPacketReceived(uint64_t time_us, const PacketReceivedData& data) {
    auto event_data = std::make_unique<PacketReceivedData>(data);
    LogEvent(time_us, QlogEvents::kPacketReceived, std::move(event_data));
}

void QlogTrace::LogMetricsUpdated(uint64_t time_us, const RecoveryMetricsData& data) {
    auto event_data = std::make_unique<RecoveryMetricsData>(data);
    LogEvent(time_us, QlogEvents::kRecoveryMetricsUpdated, std::move(event_data));
}

void QlogTrace::LogConnectionStarted(uint64_t time_us, const ConnectionStartedData& data) {
    auto event_data = std::make_unique<ConnectionStartedData>(data);
    LogEvent(time_us, QlogEvents::kConnectionStarted, std::move(event_data));
}

void QlogTrace::LogConnectionClosed(uint64_t time_us, const ConnectionClosedData& data) {
    auto event_data = std::make_unique<ConnectionClosedData>(data);
    LogEvent(time_us, QlogEvents::kConnectionClosed, std::move(event_data));
}

void QlogTrace::SetCommonFields(const CommonFields& fields) {
    // Lock-free: only called during connection initialization, single-threaded access
    // 无需加锁：只在连接初始化时调用，单线程访问
    common_fields_ = fields;
}

void QlogTrace::SetConfiguration(const QlogConfiguration& config) {
    // Lock-free: only called during connection initialization, single-threaded access
    // 无需加锁：只在连接初始化时调用，单线程访问
    configuration_ = config;
}

bool QlogTrace::ShouldLogEvent(const std::string& event_name) {
    // white list filter
    if (!config_.event_whitelist.empty()) {
        return std::find(config_.event_whitelist.begin(), config_.event_whitelist.end(), event_name) !=
               config_.event_whitelist.end();
    }

    // black list filter
    if (!config_.event_blacklist.empty()) {
        return std::find(config_.event_blacklist.begin(), config_.event_blacklist.end(), event_name) ==
               config_.event_blacklist.end();
    }

    return true;
}

void QlogTrace::WriteEvent(const QlogEvent& event) {
#ifndef NDEBUG
    // Debug mode: Verify single-thread access assumption
    // Connection should always be processed in the same thread
    if (std::this_thread::get_id() != owner_thread_id_) {
        LOG_FATAL("QlogTrace accessed from wrong thread! Connection: %s",
                  connection_id_.c_str());
    }
#endif

    if (!writer_) {
        return;
    }

    // Lock-free: Connection is bound to a single thread, no mutex needed
    // 无需加锁：连接与线程绑定，QlogTrace 只会被单线程访问

    // first write header
    if (!header_written_) {
        std::string header =
            serializer_->SerializeTraceHeader(connection_id_, vantage_point_, common_fields_, configuration_);
        writer_->WriteHeader(connection_id_, header);
        header_written_ = true;
    }

    // serialize event
    std::string serialized = serializer_->SerializeEvent(event);

    // submit to async writer
    writer_->WriteEvent(connection_id_, serialized);
}

void QlogTrace::Flush() {
    if (writer_) {
        writer_->Flush();
    }
}

}  // namespace common
}  // namespace quicx
