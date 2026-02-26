#include "common/log/log.h"

#include "http3/connection/if_connection.h"
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
}

void IConnection::Init() {
    // Start periodic cleanup timer for completed streams (runs every 100ms)
    StartCleanupTimer();
}

IConnection::~IConnection() {
    // Set flag to prevent timer callbacks from accessing this object
    is_destroying_->store(true);

    // Only call Close if the QUIC connection is still active
    // If already closing/draining/closed, skip to avoid duplicate CloseInternal error
    if (quic_connection_ && !quic_connection_->IsTerminating()) {
        Close(0);
    }
}

void IConnection::Close(uint32_t error_code) {
    // Skip if QUIC connection is already in terminating state
    if (!quic_connection_ || quic_connection_->IsTerminating()) {
        return;
    }

    if (error_code != 0) {
        quic_connection_->Reset(error_code);
    } else {
        quic_connection_->Close();
    }
}

bool IConnection::InitiateMigration() {
    if (!quic_connection_) {
        common::LOG_WARN("IConnection::InitiateMigration: no QUIC connection");
        return false;
    }
    return quic_connection_->InitiateMigration();
}

MigrationResult IConnection::InitiateMigrationTo(const std::string& local_ip, uint16_t local_port) {
    if (!quic_connection_) {
        common::LOG_WARN("IConnection::InitiateMigrationTo: no QUIC connection");
        return MigrationResult::kFailedInvalidState;
    }
    return quic_connection_->InitiateMigrationTo(local_ip, local_port);
}

void IConnection::SetMigrationCallback(migration_callback cb) {
    if (quic_connection_) {
        quic_connection_->SetMigrationCallback(cb);
    }
}

bool IConnection::IsMigrationSupported() const {
    if (!quic_connection_) {
        return false;
    }
    return quic_connection_->IsMigrationSupported();
}

bool IConnection::IsMigrationInProgress() const {
    if (!quic_connection_) {
        return false;
    }
    return quic_connection_->IsMigrationInProgress();
}

void IConnection::HandleSettings(const std::unordered_map<uint16_t, uint64_t>& settings) {
    // RFC 9114 Section 4.1: Mark SETTINGS as received
    settings_received_ = true;

    // merge settings
    for (auto iter = settings.begin(); iter != settings.end(); ++iter) {
        // RFC 9114 §7.2.4.1: IDs 0x02, 0x03, 0x04, 0x05 are reserved (HTTP/2 legacy).
        // Receipt of these MUST be treated as a connection error of type H3_SETTINGS_ERROR.
        uint16_t id = iter->first;
        if (id == 0x02 || id == 0x03 || id == 0x04 || id == 0x05) {
            common::LOG_ERROR("received forbidden HTTP/2 settings id: 0x%02x", id);
            Close(0x109);  // H3_SETTINGS_ERROR
            return;
        }
        settings_[iter->first] = std::min(settings_[iter->first], iter->second);
        common::LOG_DEBUG("settings. key:%d, value:%d", iter->first, settings_[iter->first]);
    }
}

const std::unordered_map<uint16_t, uint64_t> IConnection::AdaptSettings(const Http3Settings& settings) {
    std::unordered_map<uint16_t, uint64_t> settings_map;
    // RFC 9114 §7.2.4.1: Only valid HTTP/3 settings may be sent on the wire.
    // IDs 0x02-0x05 are forbidden (HTTP/2 legacy) and MUST NOT be sent.
    settings_map[SettingsType::kMaxFieldSectionSize] = settings.max_field_section_size;
    settings_map[SettingsType::kQpackMaxTableCapacity] = settings.qpack_max_table_capacity;
    settings_map[SettingsType::kQpackBlockedStreams] = settings.qpack_blocked_streams;
    settings_map[SettingsType::kEnableConnectProtocol] = 0;

    return settings_map;
}

void IConnection::StartCleanupTimer() {
    if (!quic_connection_) {
        return;
    }

    // Capture weak_ptr to this to safely handle timer callbacks after object destruction
    std::weak_ptr<IConnection> weak_self = weak_from_this();

    // Create a periodic timer that runs every 100ms to cleanup completed streams
    cleanup_timer_id_ = quic_connection_->AddTimer(
        [weak_self]() {
            // Check if the connection relies alive
            auto self = weak_self.lock();
            if (!self) {
                // Connection destroyed, do nothing
                return;
            }

            self->CleanupDestroyedStreams();
            // Re-schedule next cleanup
            self->StartCleanupTimer();
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
