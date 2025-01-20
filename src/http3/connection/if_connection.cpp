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
    }
}

const std::unordered_map<uint16_t, uint64_t> IConnection::AdaptSettings(const Http3Settings& settings) {
    std::unordered_map<uint16_t, uint64_t> settings_map;
    settings_map[SETTINGS_TYPE::ST_MAX_HEADER_LIST_SIZE] = settings.max_header_list_size;
    settings_map[SETTINGS_TYPE::ST_ENABLE_PUSH] = settings.enable_push;
    settings_map[SETTINGS_TYPE::ST_MAX_CONCURRENT_STREAMS] = settings.max_concurrent_streams;
    settings_map[SETTINGS_TYPE::ST_MAX_FRAME_SIZE] = settings.max_frame_size;
    settings_map[SETTINGS_TYPE::ST_MAX_FIELD_SECTION_SIZE] = settings.max_field_section_size;

    // TODO: implement below settings
    settings_map[SETTINGS_TYPE::ST_QPACK_MAX_TABLE_CAPACITY] = 0;
    settings_map[SETTINGS_TYPE::ST_QPACK_BLOCKED_STREAMS] = 0;
    settings_map[SETTINGS_TYPE::ST_ENABLE_CONNECT_PROTOCOL] = 0;

    return settings_map;
}

}
}
