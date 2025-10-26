#include "common/log/log.h"
#include "http3/http/type.h"
#include "http3/http/error.h"
#include "http3/stream/type.h"
#include "common/buffer/buffer.h"
#include "http3/connection/type.h"
#include "http3/stream/response_stream.h"
#include "http3/stream/push_sender_stream.h"
#include "http3/frame/qpack_decoder_frames.h"
#include "http3/stream/unidentified_stream.h"
#include "http3/connection/connection_server.h"
#include "http3/stream/qpack_encoder_sender_stream.h"
#include "http3/stream/qpack_decoder_sender_stream.h"
#include "http3/stream/control_client_sender_stream.h"
#include "http3/stream/qpack_encoder_receiver_stream.h"
#include "http3/stream/qpack_decoder_receiver_stream.h"
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
    max_push_id_(0),  // RFC 9114: Initially 0, wait for client's MAX_PUSH_ID frame
    next_push_id_(0),
    send_limit_push_id_(0) {

    // create control stream
    auto control_stream = quic_connection_->MakeStream(quic::StreamDirection::kSend);
    control_sender_stream_ = std::make_shared<ControlClientSenderStream>(
        std::dynamic_pointer_cast<quic::IQuicSendStream>(control_stream),
        std::bind(&ServerConnection::HandleError, this, std::placeholders::_1, std::placeholders::_2));
    
    settings_ = IConnection::AdaptSettings(settings);
    control_sender_stream_->SendSettings(settings_);
    
    // RFC 9204: QPACK is mandatory for HTTP/3, but dynamic table usage is optional
    // Create QPACK streams only if dynamic table is enabled
    bool qpack_enabled = (settings.qpack_max_table_capacity > 0 || settings.qpack_blocked_streams > 0);
    
    if (qpack_enabled) {
        common::LOG_DEBUG("ServerConnection: QPACK enabled (max_table_capacity=%llu, blocked_streams=%llu)", 
                         settings.qpack_max_table_capacity, settings.qpack_blocked_streams);
        
        // Create QPACK Encoder Stream (server -> client, type 0x02)
        auto qpack_enc_stream = quic_connection_->MakeStream(quic::StreamDirection::kSend);
        auto encoder_sender = std::make_shared<QpackEncoderSenderStream>(
            std::dynamic_pointer_cast<quic::IQuicSendStream>(qpack_enc_stream),
            std::bind(&ServerConnection::HandleError, this, std::placeholders::_1, std::placeholders::_2));
        streams_[encoder_sender->GetStreamID()] = encoder_sender;
        
        // Create QPACK Decoder Stream (server receives from client, type 0x03)
        auto qpack_dec_stream = quic_connection_->MakeStream(quic::StreamDirection::kRecv);
        auto decoder_receiver = std::make_shared<QpackDecoderReceiverStream>(
            std::dynamic_pointer_cast<quic::IQuicRecvStream>(qpack_dec_stream),
            blocked_registry_,
            std::bind(&ServerConnection::HandleError, this, std::placeholders::_1, std::placeholders::_2));
        streams_[decoder_receiver->GetStreamID()] = decoder_receiver;
        
        // Create QPACK Decoder Sender Stream (server -> client, type 0x03)
        auto qpack_dec_sender_stream = quic_connection_->MakeStream(quic::StreamDirection::kSend);
        auto decoder_sender = std::make_shared<QpackDecoderSenderStream>(
            std::dynamic_pointer_cast<quic::IQuicSendStream>(qpack_dec_sender_stream),
            std::bind(&ServerConnection::HandleError, this, std::placeholders::_1, std::placeholders::_2));
        streams_[decoder_sender->GetStreamID()] = decoder_sender;
        
        // Wire QPACK encoder to send instructions via encoder stream
        auto weak_enc = encoder_sender;
        qpack_encoder_->SetInstructionSender([weak_enc](const std::vector<std::pair<std::string,std::string>>& inserts){
            if (!weak_enc) return;
            // Encode encoder instructions (Insert, Duplicate, Set Capacity)
            uint8_t buf[2048];
            auto instr_buf = std::make_shared<common::Buffer>(buf, sizeof(buf));
            QpackEncoder enc;
            enc.EncodeEncoderInstructions(inserts, instr_buf);
            std::vector<uint8_t> blob(instr_buf->GetData(), instr_buf->GetData() + instr_buf->GetDataLength());
            weak_enc->SendInstructions(blob);
        });
        
        // Wire QPACK encoder to send decoder feedback (Section Ack, Stream Cancel, Insert Count Increment)
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
                default: break;
            }
        });
        
        common::LOG_DEBUG("ServerConnection: QPACK streams initialized (encoder, decoder)");
    } else {
        common::LOG_DEBUG("ServerConnection: QPACK disabled (no dynamic table configuration)");
    }
}

ServerConnection::~ServerConnection() {

}

bool ServerConnection::SendPush(uint64_t push_id, std::shared_ptr<IResponse> response) {
    if (streams_.size() >= settings_[SettingsType::kMaxConcurrentStreams]) {
        common::LOG_ERROR("ServerConnection::SendPush max concurrent streams reached");
        return false;
    }

    auto stream = quic_connection_->MakeStream(quic::StreamDirection::kSend);
    if (!stream) {
        common::LOG_ERROR("ServerConnection::SendPush make stream failed");
        return false;
    }

    std::shared_ptr<PushSenderStream> push_stream = std::make_shared<PushSenderStream>(qpack_encoder_,
        std::dynamic_pointer_cast<quic::IQuicSendStream>(stream),
        std::bind(&ServerConnection::HandleError, this, std::placeholders::_1, std::placeholders::_2));
    
    push_stream->SendPushResponse(push_id, response);
    return true;
}

void ServerConnection::HandleHttp(std::shared_ptr<IRequest> request, std::shared_ptr<IResponse> response, std::shared_ptr<ResponseStream> response_stream) {
    if (http_handler_) {
        http_handler_(request, response);
    }

    if (!IsEnabledPush()) {
        common::LOG_DEBUG("ServerConnection::HandleHttp push is disabled");
        return;
    }

    // check if push is enabled
    auto push_responses = response->GetPushResponses();
    for (auto& push_response : push_responses) {
        if (!CanPush()) {
            common::LOG_WARN("Cannot push: next_push_id=%llu, max_push_id=%llu", next_push_id_, max_push_id_);
            break;
        }

        // RFC 9114: Allocate push ID and send PUSH_PROMISE
        uint64_t current_push_id = next_push_id_++;
        response_stream->SendPushPromise(push_response->GetHeaders(), current_push_id);
        push_responses_[current_push_id] = push_response;
        
        common::LOG_DEBUG("PUSH_PROMISE sent with push_id=%llu", current_push_id);
    }
    if (send_limit_push_id_ < next_push_id_) {
        send_limit_push_id_ = next_push_id_;

        // wait if client cancel push
        quic_server_->AddTimer(kServerPushWaitTimeMs, std::bind(&ServerConnection::HandleTimer, this));
    }
}

void ServerConnection::HandleStream(std::shared_ptr<quic::IQuicStream> stream, uint32_t error) {
    common::LOG_DEBUG("ServerConnection::HandleStream stream. stream id: %llu, error: %d", stream->GetStreamID(), error);
    if (error != 0) {
        common::LOG_ERROR("ServerConnection::HandleStream error: %d", error);
        if (stream) {
            streams_.erase(stream->GetStreamID());
        }
        return;
    }

    // Check stream limit
    if (streams_.size() >= settings_[SettingsType::kMaxConcurrentStreams]) {
        common::LOG_ERROR("ServerConnection::HandleStream max concurrent streams reached");
        Close(Http3ErrorCode::kStreamCreationError);
        return;
    }

    if (stream->GetDirection() == quic::StreamDirection::kBidi) {
        // RFC 9114: Bidirectional streams are used for HTTP requests/responses
        std::shared_ptr<ResponseStream> response_stream = std::make_shared<ResponseStream>(qpack_encoder_,
            blocked_registry_,
            std::dynamic_pointer_cast<quic::IQuicBidirectionStream>(stream),
            std::bind(&ServerConnection::HandleError, this, std::placeholders::_1, std::placeholders::_2),
            std::bind(&ServerConnection::HandleHttp, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
        streams_[response_stream->GetStreamID()] = response_stream;

    } else if (stream->GetDirection() == quic::StreamDirection::kRecv) {
        // RFC 9114 Section 6.2: All unidirectional streams begin with a stream type
        // Create an UnidentifiedStream to read the stream type first
        auto recv_stream = std::dynamic_pointer_cast<quic::IQuicRecvStream>(stream);
        auto unidentified = std::make_shared<UnidentifiedStream>(
            recv_stream,
            std::bind(&ServerConnection::HandleError, this, std::placeholders::_1, std::placeholders::_2),
            [this](uint64_t stream_type, std::shared_ptr<quic::IQuicRecvStream> s, const std::vector<uint8_t>& remaining_data) {
                this->OnStreamTypeIdentified(stream_type, s, remaining_data);
            });
        
        // Store temporarily until stream type is identified
        streams_[stream->GetStreamID()] = unidentified;
    }
}

void ServerConnection::OnStreamTypeIdentified(
    uint64_t stream_type, 
    std::shared_ptr<quic::IQuicRecvStream> stream,
    const std::vector<uint8_t>& remaining_data) {
    
    common::LOG_DEBUG("ServerConnection: stream type %llu identified for stream %llu", 
                     stream_type, stream->GetStreamID());
    
    // Remove the temporary UnidentifiedStream
    streams_.erase(stream->GetStreamID());
    
    std::shared_ptr<IStream> typed_stream;
    
    switch (stream_type) {
        case static_cast<uint64_t>(StreamType::kControl): // Control Stream (RFC 9114 Section 6.2.1)
            common::LOG_DEBUG("ServerConnection: creating Control Stream for stream %llu", stream->GetStreamID());
            typed_stream = std::make_shared<ControlServerReceiverStream>(
                stream,
                std::bind(&ServerConnection::HandleError, this, std::placeholders::_1, std::placeholders::_2),
                std::bind(&ServerConnection::HandleGoaway, this, std::placeholders::_1),
                std::bind(&ServerConnection::HandleSettings, this, std::placeholders::_1),
                std::bind(&ServerConnection::HandleMaxPushId, this, std::placeholders::_1),
                std::bind(&ServerConnection::HandleCancelPush, this, std::placeholders::_1));
            break;
            
        case static_cast<uint64_t>(StreamType::kPush): // Push Stream (RFC 9114 Section 4.6)
            // RFC 9114: Clients MUST NOT send push streams to servers
            common::LOG_ERROR("ServerConnection: received push stream from client (protocol violation) on stream %llu", 
                            stream->GetStreamID());
            HandleError(stream->GetStreamID(), Http3ErrorCode::kStreamCreationError);
            return;
            
        case static_cast<uint64_t>(StreamType::kQpackEncoder): // QPACK Encoder Stream (RFC 9204 Section 4.2)
            common::LOG_DEBUG("ServerConnection: creating QPACK Encoder Receiver Stream for stream %llu", stream->GetStreamID());
            typed_stream = std::make_shared<QpackEncoderReceiverStream>(
                stream,
                blocked_registry_,
                std::bind(&ServerConnection::HandleError, this, std::placeholders::_1, std::placeholders::_2));
            break;
            
        case static_cast<uint64_t>(StreamType::kQpackDecoder): // QPACK Decoder Stream (RFC 9204 Section 4.2)
            common::LOG_DEBUG("ServerConnection: creating QPACK Decoder Receiver Stream for stream %llu", stream->GetStreamID());
            typed_stream = std::make_shared<QpackDecoderReceiverStream>(
                stream,
                blocked_registry_,
                std::bind(&ServerConnection::HandleError, this, std::placeholders::_1, std::placeholders::_2));
            break;
            
        default:
            // RFC 9114 Section 6.2: Unknown stream types MUST be ignored
            common::LOG_WARN("ServerConnection: unknown stream type %llu on stream %llu, ignoring", 
                           stream_type, stream->GetStreamID());
            return;
    }
    
    if (typed_stream) {
        streams_[stream->GetStreamID()] = typed_stream;
        
        // Feed remaining data to the new stream if any
        if (!remaining_data.empty()) {
            common::LOG_DEBUG("ServerConnection: feeding %zu bytes of remaining data to stream %llu", 
                            remaining_data.size(), stream->GetStreamID());
            // TODO: Need to trigger OnData on the new stream with remaining_data
            // This requires adding a method to inject initial data
        }
    }
}

void ServerConnection::HandleGoaway(uint64_t id) {
    // TODO: implement goaway
    Close(0);
}

void ServerConnection::HandleMaxPushId(uint64_t max_push_id) {
    // RFC 9114 Section 7.2.7: MAX_PUSH_ID cannot be reduced
    if (max_push_id < max_push_id_) {
        common::LOG_ERROR("MAX_PUSH_ID cannot be reduced: old=%llu, new=%llu", max_push_id_, max_push_id);
        Close(Http3ErrorCode::kIdError);
        return;
    }
    
    common::LOG_DEBUG("MAX_PUSH_ID updated: %llu -> %llu", max_push_id_, max_push_id);
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
            uint64_t push_id = iter->first;
            common::LOG_DEBUG("Sending delayed push with push_id=%llu", push_id);
            SendPush(push_id, iter->second);
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
    // RFC 9114 Section 7.2.7: MAX_PUSH_ID specifies the maximum push ID 
    // that the server can use. A value of 0 allows push ID 0.
    // The server can use push IDs from 0 to max_push_id_ (inclusive).
    if (next_push_id_ > max_push_id_) {
        return false;
    }
    
    // Check for Push ID overflow (unlikely but theoretically possible)
    if (next_push_id_ == UINT64_MAX) {
        common::LOG_ERROR("Push ID reached maximum value (UINT64_MAX)");
        return false;
    }
    
    return true;
}

}
}
