#include "common/log/log.h"
#include "http3/http/response.h"
#include "http3/stream/response_stream.h"
#include "http3/connection/server_connection.h"
#include "http3/stream/control_server_receiver_stream.h"

namespace quicx {
namespace http3 {

ServerConnection::ServerConnection(const std::shared_ptr<quic::IQuicConnection>& quic_connection,
    const std::function<void(int32_t error)>& error_handler,
    const http_handler& http_handler):
    error_handler_(error_handler),
    http_handler_(http_handler),
    quic_connection_(quic_connection),
    max_push_id_(0) {

    // create control streams
    auto control_stream = quic_connection_->MakeStream(quic::SD_SEND);
    control_sender_stream_ = std::make_shared<ControlSenderStream>(
        std::dynamic_pointer_cast<quic::IQuicSendStream>(control_stream),
        std::bind(&ServerConnection::HandleError, this, std::placeholders::_1, std::placeholders::_2));

    quic_connection_->SetStreamStateCallBack(std::bind(&ServerConnection::HandleStream, this, 
        std::placeholders::_1, std::placeholders::_2));
    
    qpack_encoder_ = std::make_shared<QpackEncoder>();
}

ServerConnection::~ServerConnection() {
    Close(0);
}

void ServerConnection::Close(uint64_t error_code) {
    if (quic_connection_) {
        quic_connection_->Close();
        quic_connection_.reset();
    }
}

bool ServerConnection::SendPushPromise(const std::unordered_map<std::string, std::string>& headers) {
    if (max_push_id_ == 0) {
        return false;
    }

    // TODO: implement push promise
    return true;
}

bool ServerConnection::SendPush(const IResponse& response) {
    if (max_push_id_ == 0) {
        return false;
    }

    // TODO: implement push
    return true;
}

void ServerConnection::HandleStream(std::shared_ptr<quic::IQuicStream> stream, uint32_t error) {
    if (error != 0) {
        common::LOG_ERROR("ServerConnection::HandleStream error: %d", error);
        return;
    }

    if (stream->GetDirection() == quic::SD_BIDI) {
        // request stream
        std::shared_ptr<ResponseStream> response_stream = std::make_shared<ResponseStream>(qpack_encoder_,
            std::dynamic_pointer_cast<quic::IQuicBidirectionStream>(stream),
            std::bind(&ServerConnection::HandleError, this, std::placeholders::_1, std::placeholders::_2),
            http_handler_);
        streams_[response_stream->GetStreamID()] = response_stream;

    } else if (stream->GetDirection() == quic::SD_RECV) {
        // control stream
        std::shared_ptr<ControlServerReceiverStream> control_stream = std::make_shared<ControlServerReceiverStream>(
            std::dynamic_pointer_cast<quic::IQuicRecvStream>(stream),
            std::bind(&ServerConnection::HandleError, this, std::placeholders::_1, std::placeholders::_2),
            std::bind(&ServerConnection::HandleGoaway, this, std::placeholders::_1),
            std::bind(&ServerConnection::HandleSettings, this, std::placeholders::_1),
            std::bind(&ServerConnection::HandleMaxPushId, this, std::placeholders::_1),
            std::bind(&ServerConnection::HandleCancelPush, this, std::placeholders::_1));
        streams_[control_stream->GetStreamID()] = control_stream;
    }
}

void ServerConnection::HandleGoaway(uint64_t id) {
    // TODO: implement goaway
}

void ServerConnection::HandleSettings(const std::unordered_map<uint16_t, uint64_t>& settings) {
    settings_ = settings;
}

void ServerConnection::HandleMaxPushId(uint64_t max_push_id) {
    max_push_id_ = max_push_id;
}

void ServerConnection::HandleCancelPush(uint64_t push_id) {
    // TODO: implement cancel push
}

void ServerConnection::HandleError(uint64_t stream_id, int32_t error) {
    if (error == 0) {
        // stream is closed by peer
        streams_.erase(stream_id);
        return;
    }

    // something wrong, notify error handler
    if (error_handler_) {
        error_handler_(error);
    }
}


}
}
