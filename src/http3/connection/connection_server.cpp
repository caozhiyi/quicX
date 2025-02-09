#include "common/log/log.h"
#include "http3/http/error.h"
#include "http3/http/config.h"
#include "http3/connection/type.h"
#include "http3/stream/response_stream.h"
#include "http3/stream/push_sender_stream.h"
#include "http3/connection/connection_server.h"
#include "http3/stream/control_server_receiver_stream.h"

namespace quicx {
namespace http3 {

ServerConnection::ServerConnection(const std::string& unique_id,
    const Http3Settings& settings,
    std::shared_ptr<quic::IQuicServer> quic_server,
    const std::shared_ptr<quic::IQuicConnection>& quic_connection,
    const std::function<void(const std::string& unique_id, uint32_t error_code)>& error_handler,
    const http_handler& http_handler):
    IConnection(unique_id, quic_connection, error_handler),
    quic_server_(quic_server),
    http_handler_(http_handler),
    max_push_id_(0) {

    // create control streams
    auto control_stream = quic_connection_->MakeStream(quic::SD_SEND);
    control_sender_stream_ = std::make_shared<ControlClientSenderStream>(
        std::dynamic_pointer_cast<quic::IQuicSendStream>(control_stream),
        std::bind(&ServerConnection::HandleError, this, std::placeholders::_1, std::placeholders::_2));
    
    settings_ = IConnection::AdaptSettings(settings);
    control_sender_stream_->SendSettings(settings_);
}

ServerConnection::~ServerConnection() {
    Close(0);
}

bool ServerConnection::SendPush(std::shared_ptr<IResponse> response) {
    if (streams_.size() >= settings_[SettingsType::kMaxConcurrentStreams]) {
        common::LOG_ERROR("ServerConnection::SendPush max concurrent streams reached");
        return false;
    }

    auto stream = quic_connection_->MakeStream(quic::SD_SEND);
    if (!stream) {
        common::LOG_ERROR("ServerConnection::SendPush make stream failed");
        return false;
    }

    std::shared_ptr<PushSenderStream> push_stream = std::make_shared<PushSenderStream>(qpack_encoder_,
        std::dynamic_pointer_cast<quic::IQuicSendStream>(stream),
        std::bind(&ServerConnection::HandleError, this, std::placeholders::_1, std::placeholders::_2));
    
    push_stream->SendPushResponse(response);
    return true;
}

void ServerConnection::HandleHttp(std::shared_ptr<IRequest> request, std::shared_ptr<IResponse> response, std::shared_ptr<ResponseStream> response_stream) {
    if (http_handler_) {
        http_handler_(request, response);
    }

    if (!IsEnabledPush()) {
        return;
    }

    // check if push is enabled
    auto push_responses = response->GetPushResponses();
    for (auto& push_response : push_responses) {
        if (!CanPush()) {
            break;
        }

        // send push promise
        response_stream->SendPushPromise(push_response->GetHeaders(), next_push_id_++);
        push_responses_[next_push_id_] = push_response;
    }
    if (send_limit_push_id_ < next_push_id_) {
        send_limit_push_id_ = next_push_id_;

        // wait if client cancel push
        quic_server_->AddTimer(kServerPushWaitTimeMs, std::bind(&ServerConnection::HandleTimer, this));
    }
}

void ServerConnection::HandleStream(std::shared_ptr<quic::IQuicStream> stream, uint32_t error) {
    if (error != 0) {
        common::LOG_ERROR("ServerConnection::HandleStream error: %d", error);
        if (stream) {
            streams_.erase(stream->GetStreamID());
        }
        return;
    }

    // TODO: implement stand line to create stream
    if (streams_.size() >= settings_[SettingsType::kMaxConcurrentStreams]) {
        common::LOG_ERROR("ServerConnection::HandleStream max concurrent streams reached");
        Close(Http3ErrorCode::kStreamCreationError);
        return;
    }

    if (stream->GetDirection() == quic::SD_BIDI) {
        // request stream
        std::shared_ptr<ResponseStream> response_stream = std::make_shared<ResponseStream>(qpack_encoder_,
            std::dynamic_pointer_cast<quic::IQuicBidirectionStream>(stream),
            std::bind(&ServerConnection::HandleError, this, std::placeholders::_1, std::placeholders::_2),
            std::bind(&ServerConnection::HandleHttp, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
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
    Close(0);
}

void ServerConnection::HandleMaxPushId(uint64_t max_push_id) {
    max_push_id_ = max_push_id;
}

void ServerConnection::HandleCancelPush(uint64_t push_id) {
    push_responses_.erase(push_id);
}

void ServerConnection::HandleError(uint64_t stream_id, uint32_t error) {
    if (error == 0) {
        // stream is closed by peer
        streams_.erase(stream_id);
        return;
    }

    // something wrong, notify error handler
    if (error_handler_) {
        error_handler_(unique_id_, error);
    }
}

void ServerConnection::HandleTimer() {
    for (auto iter = push_responses_.begin(); iter != push_responses_.end();) {
        if (iter->first < send_limit_push_id_) {
            SendPush(iter->second);
            iter = push_responses_.erase(iter);
            continue;
        }
        break;
    }
}

bool ServerConnection::IsEnabledPush() const {
    return settings_.find(SettingsType::kEnablePush) != settings_.end()
        && settings_.at(SettingsType::kEnablePush) == 1;
}

bool ServerConnection::CanPush() const {
    return next_push_id_ < max_push_id_;
}

}
}
