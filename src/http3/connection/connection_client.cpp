#include <quicx/common/metrics.h>
#include <quicx/quic/if_quic_stream.h>

#include "common/log/log.h"
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

namespace quicx {
namespace http3 {

ClientConnection::ClientConnection(const std::string& unique_id, const Http3Settings& settings,
    const std::shared_ptr<IQuicConnection>& quic_connection,
    const std::function<void(const std::string& unique_id, uint32_t error_code)>& error_handler,
    const std::function<bool(std::unordered_map<std::string, std::string>& headers)>& push_promise_handler,
    const http_response_handler& push_handler, uint64_t max_concurrent_streams, bool enable_push):
    IConnection(unique_id, settings, quic_connection, error_handler, max_concurrent_streams, enable_push),
    push_handler_(push_handler),
    push_promise_handler_(push_promise_handler) {
    // All stream/QPACK wiring is deferred to Init() so we can capture
    // weak_from_this() safely (see ownership_and_memory.md §3.1 / §5).
}

ClientConnection::~ClientConnection() {}

void ClientConnection::Init() {
    // Base Init() wires the stream-state callback, assembles the control
    // stream (SETTINGS) and the QPACK encoder/decoder sender streams.
    IConnection::Init();

    // Send MAX_PUSH_ID if push is enabled (RFC 9114 Section 7.2.7).
    if (enable_push_) {
        // Set a reasonable limit for concurrent pushes (100 is a common default)
        control_sender_stream_->SendMaxPushId(100);
        advertised_max_push_id_ = 100;
    }
}

void ClientConnection::CreateAndSendRequestStream(
    std::shared_ptr<IRequest> request, std::shared_ptr<IQuicStream> stream, const http_response_handler& handler) {
    // Per ownership_and_memory.md §2.2 / §5: capture weak_self in stream-side
    // callbacks. shared_from_this() here would form a self-cycle
    // (ClientConnection -> streams_ -> RequestStream -> handler ->
    //  shared_ptr<ClientConnection>); raw |this| would dangle if a queued
    // callback fires after ~ClientConnection() (the original __cxa_pure_virtual
    // SIGABRT). weak_ptr fixes both.
    auto weak_self = WeakSelfAs<ClientConnection>();
    auto push_promise_cb = [weak_self](std::unordered_map<std::string, std::string>& headers, uint64_t push_id) {
        auto self = weak_self.lock();
        if (!self) {
            return;
        }
        self->HandlePushPromise(headers, push_id);
    };
    std::shared_ptr<RequestStream> request_stream = std::make_shared<RequestStream>(qpack_encoder_, qpack_decoder_,
        blocked_registry_, std::dynamic_pointer_cast<IQuicBidirectionStream>(stream), handler, MakeErrorHandler(),
        std::move(push_promise_cb));
    request_stream->Init();  // Must be called after construction to set up callbacks

    // Enforce advertised SETTINGS_MAX_FIELD_SECTION_SIZE + qlog propagation.
    WireReqRespStream(request_stream);

    streams_[stream->GetStreamID()] = request_stream;

    // Metrics: HTTP/3 request started
    Metrics::CounterInc(common::MetricsStd::Http3RequestsTotal);
    Metrics::GaugeInc(common::MetricsStd::Http3RequestsActive);

    // Metrics: Record request start time
    request_start_times_[stream->GetStreamID()] = common::UTCTimeMsec();

    request_stream->SendRequest(request);
}

void ClientConnection::CreateAndSendRequestStream(std::shared_ptr<IRequest> request,
    std::shared_ptr<IQuicStream> stream, std::shared_ptr<IAsyncClientHandler> handler) {
    // Same rationale as the sibling overload above — see ownership_and_memory.md.
    auto weak_self = WeakSelfAs<ClientConnection>();
    auto push_promise_cb = [weak_self](std::unordered_map<std::string, std::string>& headers, uint64_t push_id) {
        auto self = weak_self.lock();
        if (!self) {
            return;
        }
        self->HandlePushPromise(headers, push_id);
    };
    std::shared_ptr<RequestStream> request_stream = std::make_shared<RequestStream>(qpack_encoder_, qpack_decoder_,
        blocked_registry_, std::dynamic_pointer_cast<IQuicBidirectionStream>(stream), handler, MakeErrorHandler(),
        std::move(push_promise_cb));
    request_stream->Init();  // Must be called after construction to set up callbacks

    // Enforce advertised SETTINGS_MAX_FIELD_SECTION_SIZE + qlog propagation.
    WireReqRespStream(request_stream);

    streams_[stream->GetStreamID()] = request_stream;

    // Metrics: HTTP/3 request started
    Metrics::CounterInc(common::MetricsStd::Http3RequestsTotal);
    Metrics::GaugeInc(common::MetricsStd::Http3RequestsActive);

    // Metrics: Record request start time
    request_start_times_[stream->GetStreamID()] = common::UTCTimeMsec();

    request_stream->SendRequest(request);
}

bool ClientConnection::DoRequest(std::shared_ptr<IRequest> request, const http_response_handler& handler) {
    // RFC 9114 §5.2: once a GOAWAY has been sent or received, the client
    // MUST NOT initiate any new requests on this connection. Refuse fast
    // on the caller's thread — no point in scheduling work on the loop
    // thread that we already know will be rejected.
    if (!IsAcceptingNewRequests()) {
        LOG_INFO("ClientConnection::DoRequest: refusing new request after GOAWAY");
        if (handler) {
            handler(nullptr, Http3ErrorCode::kRequestRejected);
        }
        return false;
    }

    // NOTE: max-concurrent-streams enforcement is performed inside the
    // MakeStreamAsync callback below, which runs on the QUIC event-loop
    // thread. We deliberately do NOT read streams_.size() here on the
    // caller's thread: every other access to streams_ (insert in
    // CreateAndSendRequestStream, erase in HandleError /
    // ScheduleStreamRemoval / cleanup timer, clear in ~IConnection) is
    // serialised on the loop thread, so a user-thread read here would
    // race with the loop's writes — confirmed by TSan
    // (if_connection.cpp:213 _M_erase vs connection_client.cpp:193 size).
    auto qc = quic_connection_.lock();
    if (!qc) {
        if (handler) {
            handler(nullptr, Http3ErrorCode::kInternalError);
        }
        return false;
    }
    auto weak_self = std::weak_ptr<IConnection>(shared_from_this());
    return qc->MakeStreamAsync(
        StreamDirection::kBidi, [weak_self, request, handler](std::shared_ptr<IQuicStream> stream) {
            auto self_base = weak_self.lock();
            if (!self_base) {
                // Connection already released, notify caller of failure
                if (handler) {
                    handler(nullptr, Http3ErrorCode::kInternalError);
                }
                return;
            }
            auto self = std::static_pointer_cast<ClientConnection>(self_base);
            if (!stream) {
                LOG_ERROR("ClientConnection::DoRequest stream creation failed after retry");
                if (handler) {
                    handler(nullptr, Http3ErrorCode::kInternalError);
                }
                return;
            }
            // Enforce the per-connection cap on the loop thread, where
            // streams_ is owned. Bouncing the check here costs at most
            // one extra branch on the slow path (the underlying
            // MakeStreamAsync already crossed onto the loop to create
            // the QUIC stream).
            if (self->streams_.size() >= self->max_concurrent_streams_) {
                LOG_ERROR("ClientConnection::DoRequest max concurrent streams reached");
                if (handler) {
                    handler(nullptr, Http3ErrorCode::kInternalError);
                }
                return;
            }
            self->CreateAndSendRequestStream(request, stream, handler);
        });
}

bool ClientConnection::DoRequest(std::shared_ptr<IRequest> request, std::shared_ptr<IAsyncClientHandler> handler) {
    // RFC 9114 §5.2 mirror of the const-handler overload above.
    if (!IsAcceptingNewRequests()) {
        LOG_INFO("ClientConnection::DoRequest(async): refusing new request after GOAWAY");
        if (handler) {
            handler->OnError(Http3ErrorCode::kRequestRejected);
        }
        return false;
    }

    // See comment on the const-handler overload above: the streams_.size()
    // gate runs on the loop thread, not on the user thread.
    auto qc = quic_connection_.lock();
    if (!qc) {
        if (handler) {
            handler->OnError(Http3ErrorCode::kInternalError);
        }
        return false;
    }
    auto weak_self = std::weak_ptr<IConnection>(shared_from_this());
    return qc->MakeStreamAsync(
        StreamDirection::kBidi, [weak_self, request, handler](std::shared_ptr<IQuicStream> stream) {
            auto self_base = weak_self.lock();
            if (!self_base) {
                if (handler) {
                    handler->OnError(Http3ErrorCode::kInternalError);
                }
                return;
            }
            auto self = std::static_pointer_cast<ClientConnection>(self_base);
            if (!stream) {
                LOG_ERROR("ClientConnection::DoRequest stream creation failed after retry");
                if (handler) {
                    handler->OnError(Http3ErrorCode::kInternalError);
                }
                return;
            }
            if (self->streams_.size() >= self->max_concurrent_streams_) {
                LOG_ERROR("ClientConnection::DoRequest max concurrent streams reached");
                if (handler) {
                    handler->OnError(Http3ErrorCode::kInternalError);
                }
                return;
            }
            self->CreateAndSendRequestStream(request, stream, handler);
        });
}

void ClientConnection::SetMaxPushID(uint64_t max_push_id) {
    control_sender_stream_->SendMaxPushId(max_push_id);
    // Track the high-water mark so a future GOAWAY uses a non-increasing id.
    if (max_push_id > advertised_max_push_id_) {
        advertised_max_push_id_ = max_push_id;
    }
}

void ClientConnection::CancelPush(uint64_t push_id) {
    control_sender_stream_->SendCancelPush(push_id);
}

void ClientConnection::HandleStream(std::shared_ptr<IQuicStream> stream, uint32_t error_code) {
    // Error path + bidi concurrency limit are shared with the server.
    if (HandleStreamCommon(stream, error_code)) {
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
            LOG_ERROR(
                "ClientConnection: received bidirectional stream from server (protocol violation), stream id: %llu",
                stream_id);
            auto qc = quic_connection_.lock();
            if (qc) {
                qc->Reset(Http3ErrorCode::kStreamCreationError);
            }
            return;
        } else {
            // This is a client-initiated stream that was already closed
            // Likely receiving retransmitted or out-of-order data for a closed stream
            // Silently ignore it - the stream is already cleaned up
            LOG_DEBUG(
                "ClientConnection: received data for already-closed client-initiated stream %llu, ignoring", stream_id);
            return;
        }
    }

    if (stream->GetDirection() == StreamDirection::kRecv) {
        // RFC 9114 Section 6.2: unidirectional streams start with a stream
        // type byte; wrap in an UnidentifiedStream until it arrives.
        AttachUnidentifiedStream(stream);
    }
}

std::shared_ptr<IRecvStream> ClientConnection::CreateTypedStream(
    uint64_t stream_type, const std::shared_ptr<IQuicRecvStream>& stream) {
    switch (stream_type) {
        case static_cast<uint64_t>(StreamType::kControl):  // Control Stream (RFC 9114 Section 6.2.1)
            LOG_DEBUG("ClientConnection: creating Control Stream for stream %llu", stream->GetStreamID());
            return std::make_shared<ControlReceiverStream>(
                stream, qpack_decoder_, MakeErrorHandler(), MakeGoawayHandler(), MakeSettingsHandler());

        case static_cast<uint64_t>(StreamType::kPush):  // Push Stream (RFC 9114 Section 4.6)
            LOG_DEBUG("ClientConnection: creating Push Stream for stream %llu", stream->GetStreamID());
            return std::make_shared<PushReceiverStream>(qpack_decoder_, stream, MakeErrorHandler(), push_handler_);

        default:
            // QPACK receiver streams + unknown types are role-agnostic.
            return IConnection::CreateTypedStream(stream_type, stream);
    }
}

uint64_t ClientConnection::ComputeGoawayId() {
    // RFC 9114 §5.2: client's GOAWAY id is the largest push id it will
    // accept. Pin it to the largest value we've already advertised via
    // MAX_PUSH_ID so the GOAWAY id never increases (§5.2 forbids that).
    return advertised_max_push_id_;
}

void ClientConnection::HandleError(uint64_t stream_id, uint32_t error_code) {
    // Metrics: Calculate and record request duration
    auto it = request_start_times_.find(stream_id);
    if (it != request_start_times_.end()) {
        uint64_t duration_us = (common::UTCTimeMsec() - it->second) * 1000;
        Metrics::GaugeSet(common::MetricsStd::Http3RequestDurationUs, duration_us);
        request_start_times_.erase(it);
    }

    if (error_code == 0 || error_code == static_cast<uint32_t>(Http3ErrorCode::kNoError)) {
        // Stream completed normally or closed gracefully (H3_NO_ERROR from STOP_SENDING)
        // In HTTP/3, server sends STOP_SENDING + H3_NO_ERROR after receiving the full request.
        // This is not an error - just schedule stream removal.

        // Metrics: HTTP/3 request completed successfully
        Metrics::GaugeDec(common::MetricsStd::Http3RequestsActive);

        ScheduleStreamRemoval(stream_id);
        return;
    }

    // Metrics: HTTP/3 request failed
    Metrics::GaugeDec(common::MetricsStd::Http3RequestsActive);
    Metrics::CounterInc(common::MetricsStd::Http3RequestsFailed);

    // something wrong, notify error handler
    if (error_handler_) {
        error_handler_(unique_id_, error_code);
    }
}

void ClientConnection::HandlePushPromise(std::unordered_map<std::string, std::string>& headers, uint64_t push_id) {
    // Metrics: Push promise received
    Metrics::CounterInc(common::MetricsStd::Http3PushPromisesRx);

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
