#include "common/log/log.h"
#include "http3/stream/request_stream.h"
#include "http3/stream/control_receiver_stream.h"
#include "http3/stream/push_receiver_stream.h"
#include "http3/connection/client_connection.h"

namespace quicx {
namespace http3 {

ClientConnection::ClientConnection(const std::shared_ptr<quic::IQuicConnection>& quic_connection,
    const std::function<void(uint32_t error)>& error_handler,
    const std::function<void(std::unordered_map<std::string, std::string>&)>& push_promise_handler,
    const http_response_handler& push_handler):
    error_handler_(error_handler),
    push_promise_handler_(push_promise_handler),
    push_handler_(push_handler),
    quic_connection_(quic_connection) {

    // create control streams
    auto control_stream = quic_connection_->MakeStream(quic::SD_SEND);
    control_sender_stream_ = std::make_shared<ControlClientSenderStream>(
        std::dynamic_pointer_cast<quic::IQuicSendStream>(control_stream),
        std::bind(&ClientConnection::HandleError, this, std::placeholders::_1, std::placeholders::_2));

    quic_connection_->SetStreamStateCallBack(std::bind(&ClientConnection::HandleStream, this, 
        std::placeholders::_1, std::placeholders::_2));
    
    qpack_encoder_ = std::make_shared<QpackEncoder>();
}

ClientConnection::~ClientConnection() {
    Close(0);
}

void ClientConnection::Close(uint64_t error_code) {
    if (quic_connection_) {
        quic_connection_->Close();
        quic_connection_.reset();
    }
}

bool ClientConnection::DoRequest(const std::string& url, const IRequest& request, const http_response_handler& handler) {
    // create request stream
    auto stream = quic_connection_->MakeStream(quic::SD_BIDI);
    if (!stream) {
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

void ClientConnection::HandleStream(std::shared_ptr<quic::IQuicStream> stream, uint32_t error) {
    if (error != 0) {
        common::LOG_ERROR("IStream::OnData error: %d", error);
        return;
    }

    if (stream->GetDirection() == quic::SD_BIDI) {
        // TODO: error handling
    } else if (stream->GetDirection() == quic::SD_RECV) {
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

void ClientConnection::HandleSettings(const std::unordered_map<uint16_t, uint64_t>& settings) {
    settings_ = settings;
}

void ClientConnection::HandleGoaway(uint64_t id) {
    common::LOG_INFO("ClientConnection::HandleGoaway id: %llu", id);
    Close(0);
}

void ClientConnection::HandleError(uint64_t stream_id, uint32_t error) {
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

void ClientConnection::HandlePushPromise(std::unordered_map<std::string, std::string>& headers) {
    if (push_promise_handler_) {
        push_promise_handler_(headers);
    }
}

}
}
