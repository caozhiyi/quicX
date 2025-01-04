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

    // create control streams
    auto control_stream = quic_connection_->MakeStream(quic::SD_SEND);
    control_sender_stream_ = std::make_shared<ControlClientSenderStream>(
        std::dynamic_pointer_cast<quic::IQuicSendStream>(control_stream),
        std::bind(&IConnection::HandleError, this, std::placeholders::_1, std::placeholders::_2));

    quic_connection_->SetStreamStateCallBack(std::bind(&IConnection::HandleStream, this, 
        std::placeholders::_1, std::placeholders::_2));
    
    std::unordered_map<uint16_t, uint64_t> settings;
    settings[SETTINGS_TYPE::ST_MAX_HEADER_LIST_SIZE] = 100;
    settings[SETTINGS_TYPE::ST_QPACK_MAX_TABLE_CAPACITY] = 0;
    settings[SETTINGS_TYPE::ST_QPACK_BLOCKED_STREAMS] = 0;
    settings[SETTINGS_TYPE::ST_ENABLE_PUSH] = 0;
    control_sender_stream_->SendSettings(settings);

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
    settings_ = settings;
}

}
}
