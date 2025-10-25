#include "common/log/log.h"
#include "http3/http/error.h"
#include "common/buffer/buffer.h"
#include "http3/connection/type.h"
#include "http3/stream/request_stream.h"
#include "http3/frame/qpack_decoder_frames.h"
#include "http3/stream/push_receiver_stream.h"
#include "http3/connection/connection_client.h"
#include "http3/stream/control_receiver_stream.h"
#include "http3/stream/qpack_encoder_sender_stream.h"
#include "http3/stream/qpack_decoder_receiver_stream.h"
#include "http3/stream/qpack_decoder_sender_stream.h"

namespace quicx {
namespace http3 {

ClientConnection::ClientConnection(const std::string& unique_id,
    const Http3Settings& settings,
    const std::shared_ptr<quic::IQuicConnection>& quic_connection,
    const std::function<void(const std::string& unique_id, uint32_t error_code)>& error_handler,
    const std::function<bool(std::unordered_map<std::string, std::string>& headers)>& push_promise_handler,
    const http_response_handler& push_handler):
    IConnection(unique_id, quic_connection, error_handler),
    push_promise_handler_(push_promise_handler),
    push_handler_(push_handler) {

    // create control streams and QPACK unidirectional streams
    auto control_stream = quic_connection_->MakeStream(quic::StreamDirection::kSend);
    auto qpack_enc_stream = quic_connection_->MakeStream(quic::StreamDirection::kSend);
    auto qpack_dec_stream = quic_connection_->MakeStream(quic::StreamDirection::kRecv);
    auto qpack_dec_sender_stream = quic_connection_->MakeStream(quic::StreamDirection::kSend);
    control_sender_stream_ = std::make_shared<ControlClientSenderStream>(
        std::dynamic_pointer_cast<quic::IQuicSendStream>(control_stream),
        std::bind(&ClientConnection::HandleError, this, std::placeholders::_1, std::placeholders::_2));
    
    settings_ = IConnection::AdaptSettings(settings);
    control_sender_stream_->SendSettings(settings_);

    // Wire QPACK instruction sender to QPACK encoder stream
    auto encoder_sender = std::make_shared<QpackEncoderSenderStream>(
        std::dynamic_pointer_cast<quic::IQuicSendStream>(qpack_enc_stream),
        std::bind(&ClientConnection::HandleError, this, std::placeholders::_1, std::placeholders::_2));
    // create decoder receiver stream to apply inserts
    auto decoder_receiver = std::make_shared<quicx::http3::QpackDecoderReceiverStream>(
        std::dynamic_pointer_cast<quic::IQuicRecvStream>(qpack_dec_stream),
        blocked_registry_,
        std::bind(&ClientConnection::HandleError, this, std::placeholders::_1, std::placeholders::_2));
    streams_[encoder_sender->GetStreamID()] = encoder_sender;
    streams_[decoder_receiver->GetStreamID()] = decoder_receiver;

    auto weak_enc = encoder_sender;
    qpack_encoder_->SetInstructionSender([weak_enc](const std::vector<std::pair<std::string,std::string>>& inserts){
        if (!weak_enc) return;
        // serialize inserts via QPACK helper (demo format; to be replaced by RFC-compliant encoding)
        uint8_t buf[2048];
        auto instr_buf = std::make_shared<common::Buffer>(buf, sizeof(buf));
        QpackEncoder enc;
        enc.EncodeEncoderInstructions(inserts, instr_buf);
        std::vector<uint8_t> blob(instr_buf->GetData(), instr_buf->GetData() + instr_buf->GetDataLength());
        weak_enc->SendInstructions(blob);
    });

    // Wire decoder feedback sender to QPACK decoder sender stream (0x03)
    auto decoder_sender = std::make_shared<QpackDecoderSenderStream>(
        std::dynamic_pointer_cast<quic::IQuicSendStream>(qpack_dec_sender_stream),
        std::bind(&ClientConnection::HandleError, this, std::placeholders::_1, std::placeholders::_2));
    streams_[decoder_sender->GetStreamID()] = decoder_sender;
    qpack_encoder_->SetDecoderFeedbackSender([decoder_sender](uint8_t type, uint64_t value){
        if (!decoder_sender) {
            return;
        }
        switch (type) {
            case static_cast<uint8_t>(QpackDecoderInstrType::kSectionAck):
                decoder_sender->SendSectionAck(value);
                break;
            case static_cast<uint8_t>(QpackDecoderInstrType::kStreamCancellation):
                decoder_sender->SendStreamCancel(value);
                break;
            case static_cast<uint8_t>(QpackDecoderInstrType::kInsertCountInc):
                decoder_sender->SendInsertCountIncrement(value);
                break;
            default:
                break;
        }
    });
}

ClientConnection::~ClientConnection() {

}

bool ClientConnection::DoRequest(std::shared_ptr<IRequest> request, const http_response_handler& handler) {
    if (streams_.size() >= settings_[SettingsType::kMaxConcurrentStreams]) {
        common::LOG_ERROR("ClientConnection::DoRequest max concurrent streams reached");
        return false;
    }

    // create request stream
    auto stream = quic_connection_->MakeStream(quic::StreamDirection::kBidi);
    if (!stream) {
        common::LOG_ERROR("ClientConnection::DoRequest make stream error");
        return false;
    }

    std::shared_ptr<RequestStream> request_stream = std::make_shared<RequestStream>(qpack_encoder_,
        blocked_registry_,
        std::dynamic_pointer_cast<quic::IQuicBidirectionStream>(stream),
        std::bind(&ClientConnection::HandleError, this, std::placeholders::_1, std::placeholders::_2),
        handler,
        std::bind(&ClientConnection::HandlePushPromise, this, std::placeholders::_1, std::placeholders::_2));

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
    common::LOG_DEBUG("ClientConnection::HandleStream stream. stream id: %llu, error: %d", stream->GetStreamID(), error_code);
    if (error_code != 0) {
        common::LOG_ERROR("ClientConnection::HandleStream error: %d", error_code);
        if (stream) {
            streams_.erase(stream->GetStreamID());
        }
        return;
    }

    if (stream->GetDirection() == quic::StreamDirection::kBidi) {
        quic_connection_->Reset(Http3ErrorCode::kStreamCreationError);
        return;
    }

    // TODO: implement stand line to create stream
    if (streams_.size() >= settings_[SettingsType::kMaxConcurrentStreams]) {
        common::LOG_ERROR("ClientConnection::HandleStream max concurrent streams reached");
        Close(Http3ErrorCode::kStreamCreationError);
        return;
    }
    
    if (stream->GetDirection() == quic::StreamDirection::kRecv) {
        // the first server unidirectional stream id is 3
        if (stream->GetStreamID() == 3) {
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
        error_handler_(unique_id_, error_code);
    }
}

void ClientConnection::HandlePushPromise(std::unordered_map<std::string, std::string>& headers, uint64_t push_id) {
    if (!push_promise_handler_) {
        return;
    }
    bool do_recv = push_promise_handler_(headers);
    if (!do_recv) {
        CancelPush(push_id);
    }
}

}
}
