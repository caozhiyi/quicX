#include "common/log/log.h"
#include "http3/http/error.h"
#include "http3/stream/type.h"
#include "http3/connection/type.h"
#include "http3/stream/if_recv_stream.h"
#include "http3/stream/request_stream.h"
#include "http3/stream/unidentified_stream.h"
#include "http3/frame/qpack_decoder_frames.h"
#include "http3/stream/push_receiver_stream.h"
#include "http3/connection/connection_client.h"
#include "http3/stream/control_receiver_stream.h"
#include "http3/stream/qpack_decoder_sender_stream.h"
#include "http3/stream/qpack_encoder_sender_stream.h"
#include "http3/stream/qpack_encoder_receiver_stream.h"
#include "http3/stream/qpack_decoder_receiver_stream.h"

namespace quicx {
namespace http3 {

ClientConnection::ClientConnection(const std::string& unique_id,
    const Http3Settings& settings,
    const std::shared_ptr<IQuicConnection>& quic_connection,
    const std::function<void(const std::string& unique_id, uint32_t error_code)>& error_handler,
    const std::function<bool(std::unordered_map<std::string, std::string>& headers)>& push_promise_handler,
    const http_response_handler& push_handler):
    IConnection(unique_id, quic_connection, error_handler),
    push_promise_handler_(push_promise_handler),
    push_handler_(push_handler) {

    // create control stream
    auto control_stream = quic_connection_->MakeStream(StreamDirection::kSend);
    control_sender_stream_ = std::make_shared<ControlClientSenderStream>(
        std::dynamic_pointer_cast<IQuicSendStream>(control_stream),
        std::bind(&ClientConnection::HandleError, this, std::placeholders::_1, std::placeholders::_2));
    
    settings_ = IConnection::AdaptSettings(settings);
    control_sender_stream_->SendSettings(settings_);

    // Send MAX_PUSH_ID if push is enabled (RFC 9114 Section 7.2.7)
    if (settings.enable_push) {
        // Set a reasonable limit for concurrent pushes (100 is a common default)
        control_sender_stream_->SendMaxPushId(100);
    }

    // RFC 9204: QPACK is mandatory for HTTP/3, but dynamic table usage is optional
    // Create QPACK streams only if dynamic table is enabled
    bool qpack_enabled = (settings.qpack_max_table_capacity > 0 || settings.qpack_blocked_streams > 0);
    
    if (qpack_enabled) {
        common::LOG_DEBUG("ClientConnection: QPACK enabled (max_table_capacity=%llu, blocked_streams=%llu)", 
                         settings.qpack_max_table_capacity, settings.qpack_blocked_streams);
        
        // Create QPACK streams
        auto qpack_enc_stream = quic_connection_->MakeStream(StreamDirection::kSend);
        auto qpack_dec_stream = quic_connection_->MakeStream(StreamDirection::kRecv);
        auto qpack_dec_sender_stream = quic_connection_->MakeStream(StreamDirection::kSend);
        
        // Wire QPACK instruction sender to QPACK encoder stream
        auto encoder_sender = std::make_shared<QpackEncoderSenderStream>(
            std::dynamic_pointer_cast<IQuicSendStream>(qpack_enc_stream),
            std::bind(&ClientConnection::HandleError, this, std::placeholders::_1, std::placeholders::_2));
        // create decoder receiver stream to apply inserts
        auto decoder_receiver = std::make_shared<quicx::http3::QpackDecoderReceiverStream>(
            std::dynamic_pointer_cast<IQuicRecvStream>(qpack_dec_stream),
            blocked_registry_,
            std::bind(&ClientConnection::HandleError, this, std::placeholders::_1, std::placeholders::_2));
        streams_[encoder_sender->GetStreamID()] = encoder_sender;
        streams_[decoder_receiver->GetStreamID()] = decoder_receiver;

        auto weak_enc = encoder_sender;
        qpack_encoder_->SetInstructionSender([weak_enc](const std::vector<std::pair<std::string,std::string>>& inserts){
            if (!weak_enc) {
                return;
            }
            weak_enc->SendInstructions(inserts);
        });

        // Wire decoder feedback sender to QPACK decoder sender stream
        auto decoder_sender = std::make_shared<QpackDecoderSenderStream>(
            std::dynamic_pointer_cast<IQuicSendStream>(qpack_dec_sender_stream),
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

    } else {
        common::LOG_DEBUG("ClientConnection: QPACK disabled (no dynamic table configuration)");
    }
}

ClientConnection::~ClientConnection() {

}

bool ClientConnection::DoRequest(std::shared_ptr<IRequest> request, 
                                const http_response_handler& handler) {
    if (streams_.size() >= settings_[SettingsType::kMaxConcurrentStreams]) {
        common::LOG_ERROR("ClientConnection::DoRequest max concurrent streams reached");
        return false;
    }

    // create request stream
    auto stream = quic_connection_->MakeStream(StreamDirection::kBidi);
    if (!stream) {
        common::LOG_ERROR("ClientConnection::DoRequest make stream error");
        return false;
    }

    // Create request stream for complete mode
    std::shared_ptr<RequestStream> request_stream = std::make_shared<RequestStream>(qpack_encoder_,
        blocked_registry_,
        std::dynamic_pointer_cast<IQuicBidirectionStream>(stream),
        handler,
        std::bind(&ClientConnection::HandleError, this, std::placeholders::_1, std::placeholders::_2),
        std::bind(&ClientConnection::HandlePushPromise, this, std::placeholders::_1, std::placeholders::_2));

    streams_[stream->GetStreamID()] = request_stream;
    return request_stream->SendRequest(request);
}

bool ClientConnection::DoRequest(std::shared_ptr<IRequest> request, 
                                std::shared_ptr<IAsyncClientHandler> handler) {
    if (streams_.size() >= settings_[SettingsType::kMaxConcurrentStreams]) {
        common::LOG_ERROR("ClientConnection::DoRequest max concurrent streams reached");
        return false;
    }

    // create request stream
    auto stream = quic_connection_->MakeStream(StreamDirection::kBidi);
    if (!stream) {
        common::LOG_ERROR("ClientConnection::DoRequest make stream error");
        return false;
    }

    // Create request stream for async mode
    std::shared_ptr<RequestStream> request_stream = std::make_shared<RequestStream>(qpack_encoder_,
        blocked_registry_,
        std::dynamic_pointer_cast<IQuicBidirectionStream>(stream),
        handler,
        std::bind(&ClientConnection::HandleError, this, std::placeholders::_1, std::placeholders::_2),
        std::bind(&ClientConnection::HandlePushPromise, this, std::placeholders::_1, std::placeholders::_2));

    streams_[stream->GetStreamID()] = request_stream;
    return request_stream->SendRequest(request);
}

void ClientConnection::SetMaxPushID(uint64_t max_push_id) {
    control_sender_stream_->SendMaxPushId(max_push_id);
}

void ClientConnection::CancelPush(uint64_t push_id) {
    control_sender_stream_->SendCancelPush(push_id);
}

void ClientConnection::HandleStream(std::shared_ptr<IQuicStream> stream, uint32_t error_code) {
    common::LOG_DEBUG("ClientConnection::HandleStream stream. stream id: %llu, error: %d", stream->GetStreamID(), error_code);
    if (error_code != 0) {
        common::LOG_ERROR("ClientConnection::HandleStream error: %d", error_code);
        if (stream) {
            streams_.erase(stream->GetStreamID());
        }
        return;
    }

    // RFC 9114: Client MUST NOT initiate bidirectional streams
    if (stream->GetDirection() == StreamDirection::kBidi) {
        common::LOG_ERROR("ClientConnection: received bidirectional stream from server (protocol violation)");
        quic_connection_->Reset(Http3ErrorCode::kStreamCreationError);
        return;
    }

    // Check stream limit
    if (streams_.size() >= settings_[SettingsType::kMaxConcurrentStreams]) {
        common::LOG_ERROR("ClientConnection::HandleStream max concurrent streams reached");
        Close(Http3ErrorCode::kStreamCreationError);
        return;
    }
    
    if (stream->GetDirection() == StreamDirection::kRecv) {
        // RFC 9114 Section 6.2: All unidirectional streams begin with a stream type
        // Create an UnidentifiedStream to read the stream type first
        auto recv_stream = std::dynamic_pointer_cast<IQuicRecvStream>(stream);
        auto unidentified = std::make_shared<UnidentifiedStream>(
            recv_stream,
            std::bind(&ClientConnection::HandleError, this, std::placeholders::_1, std::placeholders::_2),
            [this](uint64_t stream_type, std::shared_ptr<IQuicRecvStream> s, std::shared_ptr<IBufferRead> remaining_data) {
                this->OnStreamTypeIdentified(stream_type, s, remaining_data);
            });
        
        // Store temporarily until stream type is identified
        streams_[stream->GetStreamID()] = unidentified;
    }
}

void ClientConnection::OnStreamTypeIdentified(
    uint64_t stream_type, 
    std::shared_ptr<IQuicRecvStream> stream,
    std::shared_ptr<IBufferRead> remaining_data) {
    
    common::LOG_DEBUG("ClientConnection: stream type %llu identified for stream %llu", 
                     stream_type, stream->GetStreamID());
    
    // Remove the temporary UnidentifiedStream
    streams_.erase(stream->GetStreamID());
    
    std::shared_ptr<IRecvStream> typed_stream;
    
    switch (stream_type) {
        case static_cast<uint64_t>(StreamType::kControl): // Control Stream (RFC 9114 Section 6.2.1)
            common::LOG_DEBUG("ClientConnection: creating Control Stream for stream %llu", stream->GetStreamID());
            typed_stream = std::make_shared<ControlReceiverStream>(
                stream,
                qpack_encoder_,
                std::bind(&ClientConnection::HandleError, this, std::placeholders::_1, std::placeholders::_2),
                std::bind(&ClientConnection::HandleGoaway, this, std::placeholders::_1),
                std::bind(&ClientConnection::HandleSettings, this, std::placeholders::_1));
            break;
            
        case static_cast<uint64_t>(StreamType::kPush): // Push Stream (RFC 9114 Section 4.6)
            common::LOG_DEBUG("ClientConnection: creating Push Stream for stream %llu", stream->GetStreamID());
            typed_stream = std::make_shared<PushReceiverStream>(
                qpack_encoder_,
                stream,
                std::bind(&ClientConnection::HandleError, this, std::placeholders::_1, std::placeholders::_2),
                push_handler_);
            break;
            
        case static_cast<uint64_t>(StreamType::kQpackEncoder): // QPACK Encoder Stream (RFC 9204 Section 4.2)
            common::LOG_DEBUG("ClientConnection: creating QPACK Encoder Receiver Stream for stream %llu", stream->GetStreamID());
            typed_stream = std::make_shared<QpackEncoderReceiverStream>(
                stream,
                blocked_registry_,
                std::bind(&ClientConnection::HandleError, this, std::placeholders::_1, std::placeholders::_2));
            break;
            
        case static_cast<uint64_t>(StreamType::kQpackDecoder): // QPACK Decoder Stream (RFC 9204 Section 4.2)
            common::LOG_DEBUG("ClientConnection: creating QPACK Decoder Receiver Stream for stream %llu", stream->GetStreamID());
            typed_stream = std::make_shared<QpackDecoderReceiverStream>(
                stream,
                blocked_registry_,
                std::bind(&ClientConnection::HandleError, this, std::placeholders::_1, std::placeholders::_2));
            break;
            
        default:
            // RFC 9114 Section 6.2: Unknown stream types MUST be ignored
            common::LOG_WARN("ClientConnection: unknown stream type %llu on stream %llu, ignoring", 
                           stream_type, stream->GetStreamID());
            return;
    }
    
    if (typed_stream) {
        streams_[stream->GetStreamID()] = typed_stream;
        
        // Feed remaining data to the new stream if any
        typed_stream->OnData(remaining_data, false, 0);
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
