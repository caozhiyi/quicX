#include "common/log/log.h"
#include "http3/http/error.h"
#include "http3/stream/request_stream.h"
#include "http3/stream/control_receiver_stream.h"
#include "http3/stream/push_receiver_stream.h"
#include "http3/connection/client_connection.h"

namespace quicx {
namespace http3 {

ClientConnection::ClientConnection(const std::shared_ptr<quic::IQuicConnection>& quic_connection,
    const std::function<void(uint32_t error_code)>& error_handler,
    const std::function<void(std::unordered_map<std::string, std::string>& headers)>& push_promise_handler,
    const http_response_handler& push_handler):
    IConnection(quic_connection, error_handler),
    push_promise_handler_(push_promise_handler),
    push_handler_(push_handler) {

}

ClientConnection::~ClientConnection() {
    Close(0);
}

bool ClientConnection::DoRequest(const std::string& url, const IRequest& request, const http_response_handler& handler) {
    // create request stream
    auto stream = quic_connection_->MakeStream(quic::SD_BIDI);
    if (!stream) {
        common::LOG_ERROR("ClientConnection::DoRequest make stream error");
        return false;
    }

    std::shared_ptr<RequestStream> request_stream = std::make_shared<RequestStream>(qpack_encoder_,
        std::dynamic_pointer_cast<quic::IQuicBidirectionStream>(stream),
        std::bind(&ClientConnection::HandleError, this, std::placeholders::_1, std::placeholders::_2),
        handler,
        std::bind(&ClientConnection::HandlePushPromise, this, std::placeholders::_1));

    streams_[request_stream->GetStreamID()] = request_stream;

    request_stream->SendRequest(request);
    return true;
}

void ClientConnection::SetMaxPushID(uint64_t max_push_id) {
    control_sender_stream_->SendMaxPushId(max_push_id);
}

void ClientConnection::CancelPush(uint64_t push_id) {
    control_sender_stream_->SendCancelPush(push_id);
}

void ClientConnection::HandleStream(std::shared_ptr<quic::IQuicStream> stream, uint32_t error_code) {
    if (error_code != 0) {
        common::LOG_ERROR("ClientConnection::HandleStream error: %d", error_code);
        return;
    }

    if (stream->GetDirection() == quic::SD_BIDI) {
        quic_connection_->Reset(HTTP3_ERROR_CODE::H3EC_STREAM_CREATION_ERROR);
        return;
    }
    
    if (stream->GetDirection() == quic::SD_RECV) {
        // TODO: check stream id, control stream or push stream
        if (stream->GetStreamID() == 1) {
            // control stream
            std::shared_ptr<ControlReceiverStream> control_stream = std::make_shared<ControlReceiverStream>(
                std::dynamic_pointer_cast<quic::IQuicRecvStream>(stream),
                std::bind(&ClientConnection::HandleError, this, std::placeholders::_1, std::placeholders::_2),
                std::bind(&ClientConnection::HandleGoaway, this, std::placeholders::_1),
                std::bind(&ClientConnection::HandleSettings, this, std::placeholders::_1));
            streams_[control_stream->GetStreamID()] = control_stream;
        
        } else {
            // push stream
            std::shared_ptr<PushReceiverStream> push_stream = std::make_shared<PushReceiverStream>(qpack_encoder_,
                std::dynamic_pointer_cast<quic::IQuicRecvStream>(stream),
                std::bind(&ClientConnection::HandleError, this, std::placeholders::_1, std::placeholders::_2),
                push_handler_);
            streams_[push_stream->GetStreamID()] = push_stream;
        }
    }
}

void ClientConnection::HandleGoaway(uint64_t id) {
    common::LOG_INFO("ClientConnection::HandleGoaway id: %llu", id);
    Close(0);
}

void ClientConnection::HandleError(uint64_t stream_id, uint32_t error_code) {
    if (error_code == 0) {
        // stream is closed by peer
        streams_.erase(stream_id);
        return;
    }

    // something wrong, notify error handler
    if (error_handler_) {
        error_handler_(error_code);
    }
}

void ClientConnection::HandlePushPromise(std::unordered_map<std::string, std::string>& headers) {
    if (push_promise_handler_) {
        push_promise_handler_(headers);
    }
}

}
}