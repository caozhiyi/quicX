#include "http3/connection/if_connection.h"
#include "common/log/log.h"
#include "http3/connection/type.h"

namespace quicx {
namespace http3 {

IConnection::IConnection(const std::string& unique_id, const std::shared_ptr<IQuicConnection>& quic_connection,
    const std::function<void(const std::string& unique_id, uint32_t error_code)>& error_handler):
    unique_id_(unique_id),
    error_handler_(error_handler),
    quic_connection_(quic_connection),
    cleanup_timer_id_(0),
    is_destroying_(std::make_shared<std::atomic<bool>>(false)) {
    quic_connection_->SetStreamStateCallBack(
        std::bind(&IConnection::HandleStream, this, std::placeholders::_1, std::placeholders::_2));

    qpack_encoder_ = std::make_shared<QpackEncoder>();
    blocked_registry_ = std::make_shared<QpackBlockedRegistry>();

    // Start periodic cleanup timer for completed streams (runs every 100ms)
    StartCleanupTimer();
}

IConnection::~IConnection() {
    // Set flag to prevent timer callbacks from accessing this object
    is_destroying_->store(true);

    Close(0);
}

void IConnection::Close(uint32_t error_code) {
    if (quic_connection_) {
        if (error_code != 0) {
            quic_connection_->Reset(error_code);
        } else {
            quic_connection_->Close();
        }
    }
}

void IConnection::HandleSettings(const std::unordered_map<uint16_t, uint64_t>& settings) {
    // RFC 9114 Section 4.1: Mark SETTINGS as received
    settings_received_ = true;

    // merge settings
    for (auto iter = settings.begin(); iter != settings.end(); ++iter) {
        settings_[iter->first] = std::min(settings_[iter->first], iter->second);
        common::LOG_DEBUG("settings. key:%d, value:%d", iter->first, settings_[iter->first]);
    }
}

const std::unordered_map<uint16_t, uint64_t> IConnection::AdaptSettings(const Http3Settings& settings) {
    std::unordered_map<uint16_t, uint64_t> settings_map;
    settings_map[SettingsType::kMaxHeaderListSize] = settings.max_header_list_size;
    settings_map[SettingsType::kEnablePush] = settings.enable_push;
    settings_map[SettingsType::kMaxConcurrentStreams] = settings.max_concurrent_streams;
    settings_map[SettingsType::kMaxFrameSize] = settings.max_frame_size;
    settings_map[SettingsType::kMaxFieldSectionSize] = settings.max_field_section_size;

    // TODO: implement below settings
    settings_map[SettingsType::kQpackMaxTableCapacity] = 0;
    settings_map[SettingsType::kQpackBlockedStreams] = 0;
    settings_map[SettingsType::kEnableConnectProtocol] = 0;

    return settings_map;
}

void IConnection::StartCleanupTimer() {
    if (!quic_connection_) {
        return;
    }

    // Capture the destroying flag to safely handle timer callbacks after object destruction
    auto destroying_flag = is_destroying_;

    // Create a periodic timer that runs every 100ms to cleanup completed streams
    cleanup_timer_id_ = quic_connection_->AddTimer(
        [this, destroying_flag]() {
            // Check if the connection is being destroyed
            if (destroying_flag->load()) {
                // Connection is being destroyed, do nothing
                return;
            }

            CleanupDestroyedStreams();
            // Re-schedule next cleanup
            StartCleanupTimer();
        },
        100);  // 100ms TODO, do not use fix time, loop support defer
}

void IConnection::CleanupDestroyedStreams() {
    if (!streams_to_destroy_.empty()) {
        common::LOG_DEBUG(
            "IConnection::CleanupDestroyedStreams: cleaning up %zu completed streams", streams_to_destroy_.size());
        streams_to_destroy_.clear();
    }
}

void IConnection::ScheduleStreamRemoval(uint64_t stream_id) {
    // Move stream from active map to holding area
    // This removes it from streams_ (so it won't count against limits)
    // but keeps the shared_ptr alive temporarily to prevent use-after-free
    auto iter = streams_.find(stream_id);
    if (iter != streams_.end()) {
        common::LOG_DEBUG("IConnection::ScheduleStreamRemoval: moving stream %llu to holding area", stream_id);

        // Move to holding area - this keeps the object alive until next cleanup cycle
        streams_to_destroy_.push_back(iter->second);

        // Remove from active streams map immediately
        streams_.erase(iter);
    }
}

}  // namespace http3
}  // namespace quicx
