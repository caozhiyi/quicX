#include "common/log/log.h"

#include "http3/config.h"
#include "http3/connection/connection_server.h"
#include "http3/frame/qpack_decoder_frames.h"
#include "http3/http/error.h"
#include "http3/stream/control_client_sender_stream.h"
#include "http3/stream/control_server_receiver_stream.h"
#include "http3/stream/push_sender_stream.h"
#include "http3/stream/qpack_decoder_receiver_stream.h"
#include "http3/stream/qpack_decoder_sender_stream.h"
#include "http3/stream/qpack_encoder_receiver_stream.h"
#include "http3/stream/qpack_encoder_sender_stream.h"
#include "http3/stream/response_stream.h"
#include "http3/stream/type.h"
#include "http3/stream/unidentified_stream.h"

namespace quicx {
namespace http3 {

ServerConnection::ServerConnection(const std::string& unique_id, const Http3Settings& settings,
    const std::shared_ptr<IHttpProcessor>& http_processor, std::shared_ptr<IQuicServer> quic_server,
    const std::shared_ptr<IQuicConnection>& quic_connection,
    const std::function<void(const std::string& unique_id, uint32_t error_code)>& error_handler,
    uint64_t max_concurrent_streams,
    bool enable_push):
    IConnection(unique_id, quic_connection, error_handler),
    http_processor_(http_processor),
    quic_server_(quic_server),
    pending_settings_(settings),
    max_push_id_(0),  // RFC 9114: Initially 0, wait for client's MAX_PUSH_ID frame
    next_push_id_(0),
    send_limit_push_id_(0),
    push_timer_active_(false) {  // No active timer initially
    // Store local connection limits
    max_concurrent_streams_ = max_concurrent_streams;
    enable_push_ = enable_push;
    // All stream/QPACK wiring is deferred to Init() so we can capture
    // weak_from_this() safely (see ownership_and_memory.md §3.1 / §5).
}

void ServerConnection::Init() {
    // Wire base class first: stream-state callback + cleanup timer.
    IConnection::Init();

    const Http3Settings& settings = pending_settings_;

    // create control stream
    auto control_stream = quic_connection_->MakeStream(StreamDirection::kSend);
    control_sender_stream_ =
        std::make_shared<ControlClientSenderStream>(std::dynamic_pointer_cast<IQuicSendStream>(control_stream),
            MakeErrorHandler());

    settings_ = IConnection::AdaptSettings(settings);
    control_sender_stream_->SendSettings(settings_);

    // RFC 9204: QPACK is mandatory for HTTP/3, but dynamic table usage is optional
    // Create QPACK streams only if dynamic table is enabled
    bool qpack_enabled = (settings.qpack_max_table_capacity > 0 || settings.qpack_blocked_streams > 0);

    if (qpack_enabled) {
        LOG_DEBUG("ServerConnection: QPACK enabled (max_table_capacity=%llu, blocked_streams=%llu)",
            settings.qpack_max_table_capacity, settings.qpack_blocked_streams);

        // Enable our encoder's dynamic table (will be capped by peer's SETTINGS later)
        qpack_encoder_->SetDynamicTableEnabled(true);
        qpack_encoder_->SetMaxTableCapacity(settings.qpack_max_table_capacity);

        // Set our decoder's table capacity (this is what WE advertise to the peer)
        qpack_decoder_->SetMaxTableCapacity(settings.qpack_max_table_capacity);
        qpack_decoder_->SetDynamicTableEnabled(true);

        // Set max blocked streams on the registry
        blocked_registry_->SetMaxBlockedStreams(settings.qpack_blocked_streams);

        // Create QPACK Encoder Stream (server -> client, type 0x02)
        auto qpack_enc_stream = quic_connection_->MakeStream(StreamDirection::kSend);
        auto encoder_sender =
            std::make_shared<QpackEncoderSenderStream>(std::dynamic_pointer_cast<IQuicSendStream>(qpack_enc_stream),
                MakeErrorHandler());
        streams_[encoder_sender->GetStreamID()] = encoder_sender;

        // Create QPACK Decoder Stream (server receives from client, type 0x03)
        auto qpack_dec_stream = quic_connection_->MakeStream(StreamDirection::kRecv);
        auto decoder_receiver = std::make_shared<QpackDecoderReceiverStream>(
            std::dynamic_pointer_cast<IQuicRecvStream>(qpack_dec_stream), blocked_registry_,
            MakeErrorHandler());
        streams_[decoder_receiver->GetStreamID()] = decoder_receiver;

        // Create QPACK Decoder Sender Stream (server -> client, type 0x03)
        auto qpack_dec_sender_stream = quic_connection_->MakeStream(StreamDirection::kSend);
        auto decoder_sender = std::make_shared<QpackDecoderSenderStream>(
            std::dynamic_pointer_cast<IQuicSendStream>(qpack_dec_sender_stream),
            MakeErrorHandler());
        streams_[decoder_sender->GetStreamID()] = decoder_sender;

        // Wire QPACK encoder to send instructions via encoder stream
        // Use weak_ptr to avoid circular reference (encoder lambda -> shared_ptr -> connection -> encoder)
        std::weak_ptr<QpackEncoderSenderStream> weak_enc = encoder_sender;
        qpack_encoder_->SetInstructionSender(
            [weak_enc](const std::vector<std::pair<std::string, std::string>>& inserts) {
                auto enc = weak_enc.lock();
                if (!enc) {
                    return;
                }
                enc->SendInstructions(inserts);
            });

        // Wire QPACK decoder feedback (Section Ack, Stream Cancel, Insert Count Increment)
        // This goes on qpack_decoder_ because it's the local decoder that emits feedback
        // about successfully decoded header blocks to the peer's encoder.
        qpack_decoder_->SetDecoderFeedbackSender([decoder_sender](uint8_t type, uint64_t value) {
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

        LOG_DEBUG("ServerConnection: QPACK streams initialized (encoder, decoder)");
    } else {
        LOG_DEBUG("ServerConnection: QPACK disabled (no dynamic table configuration)");
    }
}

ServerConnection::~ServerConnection() {}

bool ServerConnection::SendPush(uint64_t push_id, std::shared_ptr<IResponse> response) {
    // RFC 9114 §5.2 / §7.2.7: respect both our own drain state and the
    // client's GOAWAY-advertised max push id. We deliberately check
    // IsAcceptingNewPushes() first so the log line below is only about
    // the per-push-id cap.
    if (!IsAcceptingNewPushes()) {
        LOG_INFO(
            "ServerConnection::SendPush: refusing push_id=%llu (draining or peer GOAWAY received)",
            (unsigned long long)push_id);
        return false;
    }
    if (goaway_received_id_ != kNoGoaway && push_id >= goaway_received_id_) {
        LOG_INFO(
            "ServerConnection::SendPush: push_id=%llu >= peer GOAWAY id %llu, refusing",
            (unsigned long long)push_id, (unsigned long long)goaway_received_id_);
        return false;
    }

    if (streams_.size() >= max_concurrent_streams_) {
        LOG_ERROR("ServerConnection::SendPush max concurrent streams reached");
        return false;
    }

    auto stream = quic_connection_->MakeStream(StreamDirection::kSend);
    if (!stream) {
        LOG_ERROR("ServerConnection::SendPush make stream failed");
        return false;
    }

    std::shared_ptr<PushSenderStream> push_stream =
        std::make_shared<PushSenderStream>(qpack_encoder_, std::dynamic_pointer_cast<IQuicSendStream>(stream),
            MakeErrorHandler());

    // Save push_id to stream mapping for cancellation support
    push_stream->SetPushId(push_id);
    push_streams_[push_id] = push_stream;

    // Store stream in streams_ map (keyed by stream_id for general stream management)
    streams_[push_stream->GetStreamID()] = push_stream;

    push_stream->SendPushResponse(push_id, response);
    return true;
}

void ServerConnection::HandlePush(
    std::shared_ptr<IResponse> response, std::shared_ptr<ResponseStream> response_stream) {
    if (!IsEnabledPush()) {
        LOG_DEBUG("ServerConnection::HandleHttp push is disabled");
        return;
    }

    // check if push is enabled
    auto push_responses = response->GetPushResponses();
    for (auto& push_response : push_responses) {
        if (!CanPush()) {
            LOG_WARN("Cannot push: next_push_id=%llu, max_push_id=%llu", next_push_id_, max_push_id_);
            break;
        }

        // RFC 9114: Allocate push ID and send PUSH_PROMISE
        uint64_t current_push_id = next_push_id_;
        response_stream->SendPushPromise(push_response->GetHeaders(), current_push_id);
        push_responses_[current_push_id] = push_response;
        next_push_id_++;  // Increment for next push

        LOG_DEBUG("PUSH_PROMISE sent with push_id=%llu", current_push_id);
    }

    // If there are new push responses, update send_limit_push_id_ and start timer
    // Only start timer if not already active to avoid multiple concurrent timers
    if (send_limit_push_id_ < next_push_id_) {
        send_limit_push_id_ = next_push_id_;

        // Start timer if not already active
        // The timer waits kServerPushWaitTimeMs to allow client to send CANCEL_PUSH
        if (!push_timer_active_) {
            push_timer_active_ = true;
            // Use weak_from_this() to avoid use-after-free when connection is destroyed before timer fires
            std::weak_ptr<IConnection> weak_self = shared_from_this();
            quic_server_->AddTimer(kServerPushWaitTimeMs, [weak_self]() {
                auto self = weak_self.lock();
                if (!self) return;
                auto server_conn = std::static_pointer_cast<ServerConnection>(self);
                server_conn->HandleTimer();
            });
            LOG_DEBUG(
                "ServerConnection::HandleHttp: started push timer for push_id < %llu", send_limit_push_id_);
        } else {
            LOG_DEBUG(
                "ServerConnection::HandleHttp: push timer already active, updated send_limit_push_id_ to %llu",
                send_limit_push_id_);
        }
    }
}

void ServerConnection::HandleStream(std::shared_ptr<IQuicStream> stream, uint32_t error) {
    LOG_DEBUG(
        "ServerConnection::HandleStream stream. stream id: %llu, error: %d", stream->GetStreamID(), error);
    if (error != 0) {
        LOG_ERROR("ServerConnection::HandleStream error: %d", error);
        if (stream) {
            streams_.erase(stream->GetStreamID());
        }
        return;
    }

    // Check stream limit
    if (streams_.size() >= max_concurrent_streams_) {
        LOG_ERROR("ServerConnection::HandleStream max concurrent streams reached");
        Close(Http3ErrorCode::kStreamCreationError);
        return;
    }

    if (stream->GetDirection() == StreamDirection::kBidi) {
        // RFC 9114 §5.2: once we've sent GOAWAY (draining_) or received
        // one from the client (goaway_received_id_), we MUST refuse to
        // process new request streams whose id is ≥ our advertised
        // GOAWAY id. The simplest implementation here is to refuse ANY
        // new bidi stream while draining — clients are expected to retry
        // on a new connection. We don't slam the QUIC connection shut
        // (that would also tear in-flight streams); instead we just
        // STOP_SENDING / RESET the new stream with H3_REQUEST_REJECTED
        // (RFC 9114 §8.1) so the client can retry idempotent requests.
        if (draining_) {
            LOG_INFO(
                "ServerConnection::HandleStream: refusing new request stream %llu after GOAWAY (draining)",
                stream->GetStreamID());
            stream->Reset(Http3ErrorCode::kRequestRejected);
            return;
        }

        // Track the largest accepted bidi stream id to compute the GOAWAY
        // id later. Underflow-safe: max_seen_bidi_stream_id_ is the
        // sentinel kNoGoaway (== UINT64_MAX) until the first request.
        uint64_t sid = stream->GetStreamID();
        if (max_seen_bidi_stream_id_ == kNoGoaway || sid > max_seen_bidi_stream_id_) {
            max_seen_bidi_stream_id_ = sid;
        }

        // RFC 9114: Bidirectional streams are used for HTTP requests/responses
        // Create ResponseStream with match_handler
        // The handler will match route and return mode + wrapped handler
        // Per ownership_and_memory.md §3.1: capture weak_self for stream-side
        // callbacks so a deferred event after ~ServerConnection() is a no-op
        // rather than a UAF / pure-virtual dispatch.
        auto weak_self = WeakSelfAs<ServerConnection>();
        auto push_cb = [weak_self](std::shared_ptr<IResponse> resp,
                                   std::shared_ptr<ResponseStream> rstream) {
            auto self = weak_self.lock();
            if (!self) {
                return;
            }
            self->HandlePush(resp, rstream);
        };
        auto settings_received_cb = [weak_self]() -> bool {
            auto self = weak_self.lock();
            if (!self) {
                return false;
            }
            return self->SettingsReceived();
        };
        std::shared_ptr<ResponseStream> response_stream = std::make_shared<ResponseStream>(qpack_encoder_,
            qpack_decoder_, blocked_registry_, std::dynamic_pointer_cast<IQuicBidirectionStream>(stream), http_processor_,
            std::move(push_cb), MakeErrorHandler(), std::move(settings_received_cb));
        response_stream->Init();  // Must be called after construction to set up callbacks

        // Propagate qlog trace from QUIC connection to HTTP/3 stream
        auto qlog_trace = quic_connection_->GetQlogTrace();
        if (qlog_trace) {
            response_stream->SetQlogTrace(qlog_trace);
        }

        streams_[response_stream->GetStreamID()] = response_stream;

    } else if (stream->GetDirection() == StreamDirection::kRecv) {
        // RFC 9114 Section 6.2: All unidirectional streams begin with a stream type
        // Create an UnidentifiedStream to read the stream type first
        auto recv_stream = std::dynamic_pointer_cast<IQuicRecvStream>(stream);
        auto weak_self = WeakSelfAs<ServerConnection>();
        auto unidentified = std::make_shared<UnidentifiedStream>(recv_stream,
            MakeErrorHandler(),
            [weak_self](
                uint64_t stream_type, std::shared_ptr<IQuicRecvStream> s, std::shared_ptr<IBufferRead> remaining_data) {
                auto self = weak_self.lock();
                if (!self) {
                    return;
                }
                self->OnStreamTypeIdentified(stream_type, s, remaining_data);
            });

        // Store temporarily until stream type is identified
        streams_[stream->GetStreamID()] = unidentified;
    }
}

void ServerConnection::OnStreamTypeIdentified(
    uint64_t stream_type, std::shared_ptr<IQuicRecvStream> stream, std::shared_ptr<IBufferRead> remaining_data) {
    LOG_DEBUG(
        "ServerConnection: stream type %llu identified for stream %llu", stream_type, stream->GetStreamID());

    // Remove the temporary UnidentifiedStream
    streams_.erase(stream->GetStreamID());

    std::shared_ptr<IRecvStream> typed_stream;

    auto weak_self = WeakSelfAs<ServerConnection>();

    switch (stream_type) {
        case static_cast<uint64_t>(StreamType::kControl):  // Control Stream (RFC 9114 Section 6.2.1)
            LOG_DEBUG("ServerConnection: creating Control Stream for stream %llu", stream->GetStreamID());
            typed_stream = std::make_shared<ControlServerReceiverStream>(stream, qpack_decoder_,
                MakeErrorHandler(),
                [weak_self](uint64_t id) {
                    auto self = weak_self.lock();
                    if (!self) return;
                    self->HandleGoaway(id);
                },
                MakeSettingsHandler(),
                [weak_self](uint64_t max_push_id) {
                    auto self = weak_self.lock();
                    if (!self) return;
                    self->HandleMaxPushId(max_push_id);
                },
                [weak_self](uint64_t push_id) {
                    auto self = weak_self.lock();
                    if (!self) return;
                    self->HandleCancelPush(push_id);
                });
            break;

        case static_cast<uint64_t>(StreamType::kPush):  // Push Stream (RFC 9114 Section 4.6)
            // RFC 9114: Clients MUST NOT send push streams to servers
            LOG_ERROR("ServerConnection: received push stream from client (protocol violation) on stream %llu",
                stream->GetStreamID());
            HandleError(stream->GetStreamID(), Http3ErrorCode::kStreamCreationError);
            return;

        case static_cast<uint64_t>(StreamType::kQpackEncoder):  // QPACK Encoder Stream (RFC 9204 Section 4.2)
            LOG_DEBUG(
                "ServerConnection: creating QPACK Encoder Receiver Stream for stream %llu", stream->GetStreamID());
            // RFC 9204: Peer's encoder instructions populate our LOCAL decoder table (qpack_decoder_)
            typed_stream = std::make_shared<QpackEncoderReceiverStream>(stream, qpack_decoder_, blocked_registry_,
                MakeErrorHandler());
            break;

        case static_cast<uint64_t>(StreamType::kQpackDecoder):  // QPACK Decoder Stream (RFC 9204 Section 4.2)
            LOG_DEBUG(
                "ServerConnection: creating QPACK Decoder Receiver Stream for stream %llu", stream->GetStreamID());
            typed_stream = std::make_shared<QpackDecoderReceiverStream>(stream, blocked_registry_,
                MakeErrorHandler());
            break;

        default:
            // RFC 9114 Section 6.2: Unknown stream types MUST be ignored
            LOG_WARN("ServerConnection: unknown stream type %llu on stream %llu, ignoring", stream_type,
                stream->GetStreamID());
            return;
    }

    if (typed_stream) {
        streams_[stream->GetStreamID()] = typed_stream;

        // Feed remaining data to the new stream if any
        typed_stream->OnData(remaining_data, false, 0);
    }
}

void ServerConnection::HandleGoaway(uint64_t id) {
    // RFC 9114 §5.2: A client's GOAWAY carries the largest push ID it is
    // willing to accept. The server MUST NOT initiate new pushes with an
    // id ≥ that value. The id MUST NOT increase across multiple GOAWAYs;
    // a peer that violates this MUST be treated as H3_ID_ERROR.
    if (goaway_received_id_ != kNoGoaway && id > goaway_received_id_) {
        LOG_ERROR(
            "ServerConnection::HandleGoaway: peer GOAWAY id increased (%llu -> %llu), closing with H3_ID_ERROR",
            (unsigned long long)goaway_received_id_, (unsigned long long)id);
        Close(static_cast<uint32_t>(Http3ErrorCode::kIdError));
        return;
    }
    LOG_INFO("ServerConnection::HandleGoaway: client GOAWAY received, max_accepted_push_id=%llu",
        (unsigned long long)id);
    goaway_received_id_ = id;

    // The server still owes the client all in-flight responses. Don't
    // close here — the client will eventually close the QUIC connection,
    // or our application code will call Shutdown() to start a symmetric
    // server-side drain. Just stop emitting new pushes that violate the
    // cap; the existing CanPush() check picks this up via
    // IsAcceptingNewPushes() below.
}

bool ServerConnection::SendGoawayFrame(uint64_t goaway_id) {
    if (!control_sender_stream_) {
        LOG_WARN("ServerConnection::SendGoawayFrame: no control sender stream");
        return false;
    }
    return control_sender_stream_->SendGoaway(goaway_id);
}

uint64_t ServerConnection::ComputeGoawayId() {
    // RFC 9114 §5.2: server's GOAWAY id is the largest stream id the
    // server WILL process. Any stream id < goaway_id is either already
    // accepted or guaranteed-rejected; the client may safely retry any
    // id >= goaway_id on a new connection.
    //
    // We pick max_seen + 4 (the next client-initiated bidi id) so that
    // every previously-accepted request is "below the line". Client-bidi
    // ids are 4n (RFC 9000 §2.1), hence the +4 step.
    if (max_seen_bidi_stream_id_ == kNoGoaway) {
        // No requests processed yet: tell client we'll accept nothing.
        return 0;
    }
    return max_seen_bidi_stream_id_ + 4;
}

void ServerConnection::HandleMaxPushId(uint64_t max_push_id) {
    // RFC 9114 Section 7.2.7: MAX_PUSH_ID cannot be reduced
    if (max_push_id < max_push_id_) {
        LOG_ERROR("MAX_PUSH_ID cannot be reduced: old=%llu, new=%llu", max_push_id_, max_push_id);
        Close(Http3ErrorCode::kIdError);
        return;
    }

    LOG_DEBUG("MAX_PUSH_ID updated: %llu -> %llu", max_push_id_, max_push_id);
    max_push_id_ = max_push_id;
}

void ServerConnection::HandleCancelPush(uint64_t push_id) {
    LOG_DEBUG("ServerConnection::HandleCancelPush: cancelling push_id=%llu", push_id);

    // Validate push_id (must be less than next_push_id_)
    if (push_id >= next_push_id_) {
        LOG_WARN(
            "ServerConnection::HandleCancelPush: invalid push_id=%llu (next_push_id=%llu)", push_id, next_push_id_);
        return;
    }

    // 1. Remove pending push response (if not yet sent)
    push_responses_.erase(push_id);

    // 2. If push stream has been created, stop and reset it
    // RFC 9114 Section 7.2.3: Server MUST stop sending push stream when CANCEL_PUSH is received
    auto iter = push_streams_.find(push_id);
    if (iter != push_streams_.end()) {
        auto push_stream = iter->second;
        uint64_t stream_id = push_stream->GetStreamID();

        LOG_DEBUG(
            "ServerConnection::HandleCancelPush: resetting push stream %llu for push_id=%llu", stream_id, push_id);

        // Reset the push stream (stops transmission)
        push_stream->Reset(Http3ErrorCode::kRequestCancelled);

        // Remove from streams_ map
        streams_.erase(stream_id);

        // Remove from push_streams_ map
        push_streams_.erase(iter);
    }
}

void ServerConnection::HandleError(uint64_t stream_id, uint32_t error) {
    LOG_DEBUG("ServerConnection::HandleError: stream_id=%llu, error=%d", stream_id, error);

    auto iter = streams_.find(stream_id);
    if (iter != streams_.end()) {
        // Check if this is a push stream before erasing
        // Clean up push_streams_ mapping if this is a push stream
        if (iter->second->GetType() == StreamType::kPush) {
            auto push_stream = std::dynamic_pointer_cast<PushSenderStream>(iter->second);
            if (push_stream) {
                uint64_t push_id = push_stream->GetPushId();
                LOG_DEBUG(
                    "ServerConnection::HandleError: cleaning up push_stream mapping for push_id=%llu, stream_id=%llu",
                    push_id, stream_id);
                push_streams_.erase(push_id);
            }
        }

        if (error == 0) {
            // Stream completed normally - schedule removal to avoid use-after-free
            // The stream object may still be in use on the call stack
            ScheduleStreamRemoval(stream_id);
        } else {
            // Error case - remove immediately
            streams_.erase(iter);
        }
    }

    // something wrong, notify error handler
    if (error != 0 && error_handler_) {
        error_handler_(unique_id_, error);
    }
}

void ServerConnection::HandleTimer() {
    LOG_DEBUG(
        "ServerConnection::HandleTimer: processing push responses, send_limit_push_id_=%llu", send_limit_push_id_);

    // Reset timer flag
    push_timer_active_ = false;

    // Send all push responses that were promised but not yet sent
    // (push_id < send_limit_push_id_ means they were included in the last batch)
    // Note: We iterate through the map and check each push_id individually
    // since unordered_map doesn't maintain order
    std::vector<uint64_t> push_ids_to_send;
    for (const auto& pair : push_responses_) {
        if (pair.first < send_limit_push_id_) {
            push_ids_to_send.push_back(pair.first);
        }
    }

    // Send all collected push responses
    for (uint64_t push_id : push_ids_to_send) {
        auto iter = push_responses_.find(push_id);
        if (iter != push_responses_.end()) {
            LOG_DEBUG("ServerConnection::HandleTimer: sending push with push_id=%llu", push_id);
            SendPush(push_id, iter->second);
            push_responses_.erase(iter);
        }
    }

    // Check if there are still pending push responses that need a new timer
    // This happens if:
    // 1. There are still push responses in the map (not all were sent)
    // 2. OR new push_id were allocated during the timer wait period (next_push_id_ > send_limit_push_id_)
    //
    // Note: If push_responses_ is empty but next_push_id_ > send_limit_push_id_, it means
    // all pushes were cancelled or already sent, so we don't need a new timer.

    // Calculate the current limit for potential new push_id allocated during timer wait
    uint64_t current_next_push_id = next_push_id_;

    // Case 1: There are still pending push responses (not sent in this timer)
    // These are push_id >= send_limit_push_id_ (they were added after timer was started)
    if (!push_responses_.empty()) {
        // There are pushes waiting, need to start a new timer
        // Update send_limit_push_id_ to current_next_push_id to include all pending pushes
        send_limit_push_id_ = current_next_push_id;
        push_timer_active_ = true;
        // Use weak_from_this() to avoid use-after-free when connection is destroyed before timer fires
        std::weak_ptr<IConnection> weak_self = shared_from_this();
        quic_server_->AddTimer(kServerPushWaitTimeMs, [weak_self]() {
            auto self = weak_self.lock();
            if (!self) return;
            auto server_conn = std::static_pointer_cast<ServerConnection>(self);
            server_conn->HandleTimer();
        });
        LOG_DEBUG(
            "ServerConnection::HandleTimer: started new timer for pending push_id (limit=%llu)", send_limit_push_id_);

        // Case 2: No pending push responses, but check if new push_id were allocated during timer wait
        // This should not normally happen, but we handle it just in case
    } else if (current_next_push_id > send_limit_push_id_) {
        // All pushes were cancelled or already sent, but new push_id were allocated
        // This means new PUSH_PROMISE was sent but then cancelled, so no timer needed
        LOG_DEBUG(
            "ServerConnection::HandleTimer: next_push_id_=%llu > send_limit_push_id_=%llu, but no pending pushes",
            current_next_push_id, send_limit_push_id_);
        // Update send_limit_push_id_ to match current_next_push_id for consistency
        send_limit_push_id_ = current_next_push_id;
    }
}

bool ServerConnection::IsEnabledPush() const {
    return enable_push_;
}

bool ServerConnection::CanPush() const {
    // RFC 9114 Section 7.2.7: MAX_PUSH_ID specifies the maximum push ID
    // that the server can use. A value of 0 allows push ID 0.
    // The server can use push IDs from 0 to max_push_id_ (inclusive).
    // Use > instead of >= so that when MAX_PUSH_ID=0, push ID 0 is allowed.
    if (next_push_id_ > max_push_id_) {
        LOG_DEBUG(
            "ServerConnection::CanPush: next_push_id=%llu > max_push_id=%llu", next_push_id_, max_push_id_);
        return false;
    }

    // Check for Push ID overflow (unlikely but theoretically possible)
    if (next_push_id_ == UINT64_MAX) {
        LOG_ERROR("Push ID reached maximum value (UINT64_MAX)");
        return false;
    }

    return true;
}

}  // namespace http3
}  // namespace quicx
