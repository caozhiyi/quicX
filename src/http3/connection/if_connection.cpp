#include "common/log/log.h"
#include "http3/connection/type.h"
#include "http3/connection/if_connection.h"

namespace quicx {
namespace http3 {

IConnection::IConnection(const std::string& unique_id,
    const std::shared_ptr<quic::IQuicConnection>& quic_connection,
    const std::function<void(const std::string& unique_id, uint32_t error_code)>& error_handler):
    unique_id_(unique_id),
    error_handler_(error_handler),
    quic_connection_(quic_connection) {

    quic_connection_->SetStreamStateCallBack(std::bind(&IConnection::HandleStream, this, 
        std::placeholders::_1, std::placeholders::_2));

    qpack_encoder_ = std::make_shared<QpackEncoder>();
    blocked_registry_ = std::make_shared<QpackBlockedRegistry>();
}

IConnection::~IConnection() {
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

}
}
