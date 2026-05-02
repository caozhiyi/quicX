#include "common/log/log.h"
#include "common/metrics/metrics.h"
#include "common/metrics/metrics_std.h"
#include "common/util/time.h"

#include "http3/connection/connection_client.h"
#include "http3/connection/type.h"
#include "http3/frame/qpack_decoder_frames.h"
#include "http3/http/error.h"
#include "http3/stream/control_receiver_stream.h"
#include "http3/stream/if_recv_stream.h"
#include "http3/stream/push_receiver_stream.h"
#include "http3/stream/qpack_decoder_receiver_stream.h"
#include "http3/stream/qpack_decoder_sender_stream.h"
#include "http3/stream/qpack_encoder_receiver_stream.h"
#include "http3/stream/qpack_encoder_sender_stream.h"
#include "http3/stream/request_stream.h"
#include "http3/stream/type.h"
#include "http3/stream/unidentified_stream.h"
#include "quic/include/if_quic_stream.h"

namespace quicx {
namespace http3 {

ClientConnection::ClientConnection(const std::string& unique_id, const Http3Settings& settings,
    const std::shared_ptr<IQuicConnection>& quic_connection,
    const std::function<void(const std::string& unique_id, uint32_t error_code)>& error_handler,
    const std::function<bool(std::unordered_map<std::string, std::string>& headers)>& push_promise_handler,
    const http_response_handler& push_handler,
    uint64_t max_concurrent_streams,
    bool enable_push):
    IConnection(unique_id, quic_connection, error_handler),
    push_promise_handler_(push_promise_handler),
    push_handler_(push_handler) {
    // Store local connection limits
    max_concurrent_streams_ = max_concurrent_streams;
    enable_push_ = enable_push;

    // create control stream
    auto control_stream = quic_connection_->MakeStream(StreamDirection::kSend);
    control_sender_stream_ =
        std::make_shared<ControlClientSenderStream>(std::dynamic_pointer_cast<IQuicSendStream>(control_stream),
            std::bind(&ClientConnection::HandleError, this, std::placeholders::_1, std::placeholders::_2));

    settings_ = IConnection::AdaptSettings(settings);
    control_sender_stream_->SendSettings(settings_);

    // Send MAX_PUSH_ID if push is enabled (RFC 9114 Section 7.2.7)
    if (enable_push_) {
        // Set a reasonable limit for concurrent pushes (100 is a common default)
        control_sender_stream_->SendMaxPushId(100);
    }

    // RFC 9204: QPACK is mandatory for HTTP/3, and encoder/decoder streams MUST be created
    // even if the dynamic table capacity is 0.

    // Create QPACK streams
    // - Encoder sender: client sends QPACK encoder instructions to server
    // - Decoder sender: client sends QPACK decoder feedback (section ack, etc.) to server
    // NOTE: The QPACK decoder RECEIVER stream is NOT created here. It will be created
    // reactively via HandleStream() -> OnStreamTypeIdentified() when the server opens
    // its QPACK encoder unidirectional stream toward us.
    auto qpack_enc_stream = quic_connection_->MakeStream(StreamDirection::kSend);
    auto qpack_dec_sender_stream = quic_connection_->MakeStream(StreamDirection::kSend);

    // Wire QPACK instruction sender to QPACK encoder stream
    auto encoder_sender =
        std::make_shared<QpackEncoderSenderStream>(std::dynamic_pointer_cast<IQuicSendStream>(qpack_enc_stream),
            std::bind(&ClientConnection::HandleError, this, std::placeholders::_1, std::placeholders::_2));
    streams_[encoder_sender->GetStreamID()] = encoder_sender;

    // Use weak_ptr to avoid circular reference (encoder lambda -> shared_ptr -> connection -> encoder)
    std::weak_ptr<QpackEncoderSenderStream> weak_enc = encoder_sender;
    qpack_encoder_->SetInstructionSender([weak_enc](const std::vector<std::pair<std::string, std::string>>& inserts) {
        auto enc = weak_enc.lock();
        if (!enc) {
            return;
        }
        enc->SendInstructions(inserts);
    });

    // Wire decoder feedback sender to QPACK decoder sender stream
    auto decoder_sender =
        std::make_shared<QpackDecoderSenderStream>(std::dynamic_pointer_cast<IQuicSendStream>(qpack_dec_sender_stream),
            std::bind(&ClientConnection::HandleError, this, std::placeholders::_1, std::placeholders::_2));
    streams_[decoder_sender->GetStreamID()] = decoder_sender;
    qpack_encoder_->SetDecoderFeedbackSender([decoder_sender](uint8_t type, uint64_t value) {
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

ClientConnection::~ClientConnection() {}

void ClientConnection::CreateAndSendRequestStream(
    std::shared_ptr<IRequest> request, std::shared_ptr<IQuicStream> stream, const http_response_handler& handler) {
    std::shared_ptr<RequestStream> request_stream = std::make_shared<RequestStream>(qpack_encoder_, blocked_registry_,
        std::dynamic_pointer_cast<IQuicBidirectionStream>(stream), handler,
        std::bind(&ClientConnection::HandleError, std::static_pointer_cast<ClientConnection>(shared_from_this()),
            std::placeholders::_1, std::placeholders::_2),
        std::bind(&ClientConnection::HandlePushPromise, std::static_pointer_cast<ClientConnection>(shared_from_this()),
            std::placeholders::_1, std::placeholders::_2));
    request_stream->Init();  // Must be called after construction to set up callbacks

    // Propagate qlog trace from QUIC connection to HTTP/3 stream
    auto qlog_trace = quic_connection_->GetQlogTrace();
    if (qlog_trace) {
        request_stream->SetQlogTrace(qlog_trace);
    }

    streams_[stream->GetStreamID()] = request_stream;

    // Metrics: HTTP/3 request started
    common::Metrics::CounterInc(common::MetricsStd::Http3RequestsTotal);
    common::Metrics::GaugeInc(common::MetricsStd::Http3RequestsActive);

    // Metrics: Record request start time
    request_start_times_[stream->GetStreamID()] = common::UTCTimeMsec();

    request_stream->SendRequest(request);
}

void ClientConnection::CreateAndSendRequestStream(std::shared_ptr<IRequest> request,
    std::shared_ptr<IQuicStream> stream, std::shared_ptr<IAsyncClientHandler> handler) {
    std::shared_ptr<RequestStream> request_stream = std::make_shared<RequestStream>(qpack_encoder_, blocked_registry_,
        std::dynamic_pointer_cast<IQuicBidirectionStream>(stream), handler,
        std::bind(&ClientConnection::HandleError, std::static_pointer_cast<ClientConnection>(shared_from_this()),
            std::placeholders::_1, std::placeholders::_2),
        std::bind(&ClientConnection::HandlePushPromise, std::static_pointer_cast<ClientConnection>(shared_from_this()),
            std::placeholders::_1, std::placeholders::_2));
    request_stream->Init();  // Must be called after construction to set up callbacks

    // Propagate qlog trace from QUIC connection to HTTP/3 stream
    auto qlog_trace = quic_connection_->GetQlogTrace();
    if (qlog_trace) {
        request_stream->SetQlogTrace(qlog_trace);
    }

    streams_[stream->GetStreamID()] = request_stream;

    // Metrics: HTTP/3 request started
    common::Metrics::CounterInc(common::MetricsStd::Http3RequestsTotal);
    common::Metrics::GaugeInc(common::MetricsStd::Http3RequestsActive);

    // Metrics: Record request start time
    request_start_times_[stream->GetStreamID()] = common::UTCTimeMsec();

    request_stream->SendRequest(request);
}

bool ClientConnection::DoRequest(std::shared_ptr<IRequest> request, const http_response_handler& handler) {
    if (streams_.size() >= max_concurrent_streams_) {
        common::LOG_ERROR("ClientConnection::DoRequest max concurrent streams reached");
        return false;
    }

    auto self = std::static_pointer_cast<ClientConnection>(shared_from_this());
    return quic_connection_->MakeStreamAsync(
        StreamDirection::kBidi, [self, request, handler](std::shared_ptr<IQuicStream> stream) {
            if (!stream) {
                common::LOG_ERROR("ClientConnection::DoRequest stream creation failed after retry");
                return;
            }
            self->CreateAndSendRequestStream(request, stream, handler);
        });
}

bool ClientConnection::DoRequest(std::shared_ptr<IRequest> request, std::shared_ptr<IAsyncClientHandler> handler) {
    if (streams_.size() >= max_concurrent_streams_) {
        common::LOG_ERROR("ClientConnection::DoRequest max concurrent streams reached");
        return false;
    }

    auto self = std::static_pointer_cast<ClientConnection>(shared_from_this());
    return quic_connection_->MakeStreamAsync(
        StreamDirection::kBidi, [self, request, handler](std::shared_ptr<IQuicStream> stream) {
            if (!stream) {
                common::LOG_ERROR("ClientConnection::DoRequest stream creation failed after retry");
                return;
            }
            self->CreateAndSendRequestStream(request, stream, handler);
        });
}

void ClientConnection::SetMaxPushID(uint64_t max_push_id) {
    control_sender_stream_->SendMaxPushId(max_push_id);
}

void ClientConnection::CancelPush(uint64_t push_id) {
    control_sender_stream_->SendCancelPush(push_id);
}

void ClientConnection::HandleStream(std::shared_ptr<IQuicStream> stream, uint32_t error_code) {
    common::LOG_DEBUG(
        "ClientConnection::HandleStream stream. stream id: %llu, error: %d", stream->GetStreamID(), error_code);
    if (error_code != 0) {
        common::LOG_ERROR("ClientConnection::HandleStream error: %d", error_code);
        if (stream) {
            streams_.erase(stream->GetStreamID());
        }
        return;
    }

    // RFC 9114: Server MUST NOT initiate bidirectional streams
    if (stream->GetDirection() == StreamDirection::kBidi) {
        uint64_t stream_id = stream->GetStreamID();
        // Check if this is a server-initiated stream (stream_id & 0x1 == 1)
        // Client-initiated streams have (stream_id & 0x1 == 0)
        bool is_server_initiated = (stream_id & 0x1) == 1;

        if (is_server_initiated) {
            // True protocol violation: server initiated a bidirectional stream
            common::LOG_ERROR(
                "ClientConnection: received bidirectional stream from server (protocol violation), stream id: %llu",
                stream_id);
            quic_connection_->Reset(Http3ErrorCode::kStreamCreationError);
            return;
        } else {
            // This is a client-initiated stream that was already closed
            // Likely receiving retransmitted or out-of-order data for a closed stream
            // Silently ignore it - the stream is already cleaned up
            common::LOG_DEBUG(
                "ClientConnection: received data for already-closed client-initiated stream %llu, ignoring", stream_id);
            return;
        }
    }

    // Check stream limit
    if (streams_.size() >= max_concurrent_streams_) {
        common::LOG_ERROR("ClientConnection::HandleStream max concurrent streams reached");
        Close(Http3ErrorCode::kStreamCreationError);
        return;
    }

    if (stream->GetDirection() == StreamDirection::kRecv) {
        // RFC 9114 Section 6.2: All unidirectional streams begin with a stream type
        // Create an UnidentifiedStream to read the stream type first
        auto recv_stream = std::dynamic_pointer_cast<IQuicRecvStream>(stream);
        auto unidentified = std::make_shared<UnidentifiedStream>(recv_stream,
            std::bind(&ClientConnection::HandleError, this, std::placeholders::_1, std::placeholders::_2),
            [this](
                uint64_t stream_type, std::shared_ptr<IQuicRecvStream> s, std::shared_ptr<IBufferRead> remaining_data) {
                this->OnStreamTypeIdentified(stream_type, s, remaining_data);
            });

        // Store temporarily until stream type is identified
        streams_[stream->GetStreamID()] = unidentified;
    }
}

void ClientConnection::OnStreamTypeIdentified(
    uint64_t stream_type, std::shared_ptr<IQuicRecvStream> stream, std::shared_ptr<IBufferRead> remaining_data) {
    common::LOG_DEBUG(
        "ClientConnection: stream type %llu identified for stream %llu", stream_type, stream->GetStreamID());

    // Remove the temporary UnidentifiedStream
    streams_.erase(stream->GetStreamID());

    std::shared_ptr<IRecvStream> typed_stream;

    switch (stream_type) {
        case static_cast<uint64_t>(StreamType::kControl):  // Control Stream (RFC 9114 Section 6.2.1)
            common::LOG_DEBUG("ClientConnection: creating Control Stream for stream %llu", stream->GetStreamID());
            typed_stream = std::make_shared<ControlReceiverStream>(stream, qpack_encoder_,
                std::bind(&ClientConnection::HandleError, this, std::placeholders::_1, std::placeholders::_2),
                std::bind(&ClientConnection::HandleGoaway, this, std::placeholders::_1),
                std::bind(&ClientConnection::HandleSettings, this, std::placeholders::_1));
            break;

        case static_cast<uint64_t>(StreamType::kPush):  // Push Stream (RFC 9114 Section 4.6)
            common::LOG_DEBUG("ClientConnection: creating Push Stream for stream %llu", stream->GetStreamID());
            typed_stream = std::make_shared<PushReceiverStream>(qpack_encoder_, stream,
                std::bind(&ClientConnection::HandleError, this, std::placeholders::_1, std::placeholders::_2),
                push_handler_);
            break;

        case static_cast<uint64_t>(StreamType::kQpackEncoder):  // QPACK Encoder Stream (RFC 9204 Section 4.2)
            common::LOG_DEBUG(
                "ClientConnection: creating QPACK Encoder Receiver Stream for stream %llu", stream->GetStreamID());
            typed_stream = std::make_shared<QpackEncoderReceiverStream>(stream, qpack_encoder_, blocked_registry_,
                std::bind(&ClientConnection::HandleError, this, std::placeholders::_1, std::placeholders::_2));
            break;

        case static_cast<uint64_t>(StreamType::kQpackDecoder):  // QPACK Decoder Stream (RFC 9204 Section 4.2)
            common::LOG_DEBUG(
                "ClientConnection: creating QPACK Decoder Receiver Stream for stream %llu", stream->GetStreamID());
            typed_stream = std::make_shared<QpackDecoderReceiverStream>(stream, blocked_registry_,
                std::bind(&ClientConnection::HandleError, this, std::placeholders::_1, std::placeholders::_2));
            break;

        default:
            // RFC 9114 Section 6.2: Unknown stream types MUST be ignored
            common::LOG_WARN("ClientConnection: unknown stream type %llu on stream %llu, ignoring", stream_type,
                stream->GetStreamID());
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
    // Metrics: Calculate and record request duration
    auto it = request_start_times_.find(stream_id);
    if (it != request_start_times_.end()) {
        uint64_t duration_us = (common::UTCTimeMsec() - it->second) * 1000;
        common::Metrics::GaugeSet(common::MetricsStd::Http3RequestDurationUs, duration_us);
        request_start_times_.erase(it);
    }

    if (error_code == 0 || error_code == static_cast<uint32_t>(Http3ErrorCode::kNoError)) {
        // Stream completed normally or closed gracefully (H3_NO_ERROR from STOP_SENDING)
        // In HTTP/3, server sends STOP_SENDING + H3_NO_ERROR after receiving the full request.
        // This is not an error - just schedule stream removal.

        // Metrics: HTTP/3 request completed successfully
        common::Metrics::GaugeDec(common::MetricsStd::Http3RequestsActive);

        ScheduleStreamRemoval(stream_id);
        return;
    }

    // Metrics: HTTP/3 request failed
    common::Metrics::GaugeDec(common::MetricsStd::Http3RequestsActive);
    common::Metrics::CounterInc(common::MetricsStd::Http3RequestsFailed);

    // something wrong, notify error handler
    if (error_handler_) {
        error_handler_(unique_id_, error_code);
    }
}

void ClientConnection::HandlePushPromise(std::unordered_map<std::string, std::string>& headers, uint64_t push_id) {
    // Metrics: Push promise received
    common::Metrics::CounterInc(common::MetricsStd::Http3PushPromisesRx);

    if (!push_promise_handler_) {
        return;
    }
    bool do_recv = push_promise_handler_(headers);
    if (!do_recv) {
        CancelPush(push_id);
    }
}

}  // namespace http3
}  // namespace quicx
