#include "common/log/log.h"

#include "http3/config.h"
#include "http3/connection/if_connection.h"
#include "http3/connection/type.h"
#include "http3/frame/qpack_decoder_frames.h"
#include "http3/http/error.h"
#include "http3/stream/qpack_decoder_receiver_stream.h"
#include "http3/stream/qpack_decoder_sender_stream.h"
#include "http3/stream/qpack_encoder_receiver_stream.h"
#include "http3/stream/qpack_encoder_sender_stream.h"
#include "http3/stream/req_resp_base_stream.h"
#include "http3/stream/unidentified_stream.h"

namespace quicx {
namespace http3 {

IConnection::IConnection(const std::string& unique_id, const Http3Settings& settings,
    const std::shared_ptr<IQuicConnection>& quic_connection,
    const std::function<void(const std::string& unique_id, uint32_t error_code)>& error_handler,
    uint64_t max_concurrent_streams, bool enable_push):
    unique_id_(unique_id),
    error_handler_(error_handler),
    pending_settings_(settings),
    quic_connection_(quic_connection),
    max_concurrent_streams_(max_concurrent_streams),
    enable_push_(enable_push),
    cleanup_timer_id_(0),
    is_destroying_(std::make_shared<std::atomic<bool>>(false)) {
    qpack_encoder_ = std::make_shared<QpackEncoder>();
    qpack_decoder_ = std::make_shared<QpackDecoder>();
    blocked_registry_ = std::make_shared<QpackBlockedRegistry>();

    // NOTE: SetStreamStateCallBack is wired in Init() (not here) because
    // shared_from_this() / weak_from_this() are not available inside the
    // constructor — std::enable_shared_from_this is only initialised once
    // make_shared<T>() finishes constructing the object. Using weak_from_this()
    // here would silently capture an empty weak_ptr and every stream callback
    // would be a no-op.
}

void IConnection::Init() {
    // Wire QUIC stream-state notifications into HandleStream() via weak_self,
    // so a queued/late callback after ~IConnection() doesn't dispatch a
    // pure virtual on a half-destroyed object (was the cause of the
    // __cxa_pure_virtual SIGABRT). Per ownership_and_memory.md §3.1.
    std::weak_ptr<IConnection> weak_self = weak_from_this();
    auto qc = quic_connection_.lock();
    if (qc) {
        qc->SetStreamStateCallBack([weak_self](std::shared_ptr<IQuicStream> stream, uint32_t error_code) {
            auto self = weak_self.lock();
            if (!self) {
                return;
            }
            self->HandleStream(stream, error_code);
        });
    }

    // Role-agnostic control + QPACK stream assembly (see header).
    SetupControlAndQpackStreams();

    // Start periodic cleanup timer for completed streams (runs every 100ms)
    StartCleanupTimer();
}

void IConnection::SetupControlAndQpackStreams() {
    auto qc = quic_connection_.lock();
    const Http3Settings& settings = pending_settings_;

    // RFC 9114 §6.2.1: create our outbound control stream and send SETTINGS.
    auto control_stream = qc ? qc->MakeStream(StreamDirection::kSend) : nullptr;
    control_sender_stream_ = std::make_shared<ControlClientSenderStream>(
        std::dynamic_pointer_cast<IQuicSendStream>(control_stream), MakeErrorHandler());

    local_settings_ = AdaptSettings(settings);
    control_sender_stream_->SendSettings(local_settings_);

    // RFC 9204: QPACK is mandatory for HTTP/3 and the encoder/decoder streams
    // MUST be created even if the dynamic table capacity is 0; the settings
    // below only gate dynamic-table *usage*.
    bool qpack_enabled = (settings.qpack_max_table_capacity > 0 || settings.qpack_blocked_streams > 0);
    if (qpack_enabled) {
        LOG_DEBUG("IConnection: QPACK dynamic table enabled (max_table_capacity=%llu, blocked_streams=%llu)",
            settings.qpack_max_table_capacity, settings.qpack_blocked_streams);

        // Enable our encoder's dynamic table (will be capped by peer's SETTINGS later)
        qpack_encoder_->SetDynamicTableEnabled(true);
        qpack_encoder_->SetMaxTableCapacity(settings.qpack_max_table_capacity);

        // Set our decoder's table capacity (this is what WE advertise to the peer)
        qpack_decoder_->SetMaxTableCapacity(settings.qpack_max_table_capacity);
        qpack_decoder_->SetDynamicTableEnabled(true);

        // Set max blocked streams on the registry
        blocked_registry_->SetMaxBlockedStreams(settings.qpack_blocked_streams);
    } else {
        LOG_DEBUG("IConnection: QPACK dynamic table disabled (no dynamic table configuration)");
    }

    // QPACK encoder sender stream: carries our encoder instructions to the
    // peer. Created unconditionally per RFC 9204 §6.2 (see above).
    auto qpack_enc_stream = qc ? qc->MakeStream(StreamDirection::kSend) : nullptr;
    auto encoder_sender = std::make_shared<QpackEncoderSenderStream>(
        std::dynamic_pointer_cast<IQuicSendStream>(qpack_enc_stream), MakeErrorHandler());
    streams_[encoder_sender->GetStreamID()] = encoder_sender;
    qpack_encoder_sender_ = encoder_sender;  // retained for P1-H4a Set Capacity

    // Use weak_ptr to avoid circular reference (encoder lambda -> shared_ptr -> connection -> encoder)
    std::weak_ptr<QpackEncoderSenderStream> weak_enc = encoder_sender;
    qpack_encoder_->SetInstructionSender([weak_enc](const std::vector<std::pair<std::string, std::string>>& inserts) {
        auto enc = weak_enc.lock();
        if (!enc) {
            return;
        }
        enc->SendInstructions(inserts);
    });

    // QPACK decoder sender stream: carries our decoder feedback (Section Ack,
    // Stream Cancel, Insert Count Increment) to the peer's encoder. This goes
    // on qpack_decoder_ because it's the local decoder that emits feedback.
    auto qpack_dec_sender_stream = qc ? qc->MakeStream(StreamDirection::kSend) : nullptr;
    auto decoder_sender = std::make_shared<QpackDecoderSenderStream>(
        std::dynamic_pointer_cast<IQuicSendStream>(qpack_dec_sender_stream), MakeErrorHandler());
    streams_[decoder_sender->GetStreamID()] = decoder_sender;
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
}

IConnection::~IConnection() {
    // Set flag to prevent timer callbacks from accessing this object
    is_destroying_->store(true);

    // Only call Close if the QUIC connection is still active. The QUIC
    // connection is held weakly (see header), so when we are destroyed it may
    // already be torn down — in that case lock() returns null and there is
    // nothing left to close.
    auto qc = quic_connection_.lock();
    if (qc && !qc->IsTerminating()) {
        Close(0);
    }

    // Drop owned streams before the rest of our members destruct. Stream
    // callbacks now capture weak_ptr<IConnection> (no self-cycle), but
    // explicit early-clear here keeps destruction order obvious: a stream
    // that fires a deferred QUIC-layer callback during teardown will see
    // weak_self.lock() == nullptr and bail out cleanly.
    streams_.clear();
    streams_to_destroy_.clear();
}

void IConnection::Close(uint32_t error_code) {
    // Skip if QUIC connection is already in terminating state or gone
    auto qc = quic_connection_.lock();
    if (!qc || qc->IsTerminating()) {
        return;
    }

    if (error_code != 0) {
        qc->Reset(error_code);
    } else {
        qc->Close();
    }
}

void IConnection::Shutdown() {
    // RFC 9114 §5.2: GOAWAY may only be sent once per direction with a
    // non-increasing id. Repeated calls collapse into a single GOAWAY.
    auto qc = quic_connection_.lock();
    if (!qc || qc->IsTerminating()) {
        return;
    }
    if (draining_) {
        // Already shutting down. Per RFC the id MUST NOT increase, so we
        // never re-emit even if more requests have been processed since.
        return;
    }

    uint64_t goaway_id = ComputeGoawayId();
    // Best-effort send; if it fails, we still drain — peer will eventually
    // see CONNECTION_CLOSE when the QUIC connection closes.
    (void)SendGoawayFrame(goaway_id);
    goaway_sent_id_ = goaway_id;
    draining_ = true;

    LOG_INFO("IConnection::Shutdown: GOAWAY sent (id=%llu), entering drain", (unsigned long long)goaway_id);

    // If there's nothing in flight already, close immediately rather than
    // waiting up to 100ms for the cleanup timer tick.
    if (!HasInFlightRequests()) {
        LOG_DEBUG("IConnection::Shutdown: no in-flight requests, closing now");
        Close(0);
    }
}

bool IConnection::IsAcceptingNewRequests() const {
    // Local-initiated requests are refused as soon as we start draining
    // OR as soon as the peer told us it is going away (RFC 9114 §5.2:
    // recipient of GOAWAY SHOULD NOT initiate additional requests).
    if (draining_) {
        return false;
    }
    if (goaway_received_id_ != kNoGoaway) {
        return false;
    }
    return true;
}

bool IConnection::IsAcceptingNewPushes() const {
    // RFC 9114 §5.2 / §7.2.7: server MUST NOT promise pushes with id
    // greater than what client advertised in its GOAWAY, and MUST NOT
    // initiate new pushes once it has sent its own GOAWAY.
    if (draining_) {
        return false;
    }
    if (goaway_received_id_ != kNoGoaway) {
        return false;
    }
    return true;
}

bool IConnection::HasInFlightRequests() const {
    // Default policy: any request/response or push stream blocks drain.
    // Long-lived control/QPACK streams do NOT (they exist for the entire
    // lifetime of the connection and would deadlock the drain).
    for (const auto& kv : streams_) {
        if (!kv.second) {
            continue;
        }
        StreamType t = kv.second->GetType();
        if (t == StreamType::kReqResp || t == StreamType::kPush) {
            return true;
        }
    }
    return false;
}

bool IConnection::InitiateMigration() {
    auto qc = quic_connection_.lock();
    if (!qc) {
        LOG_WARN("IConnection::InitiateMigration: no QUIC connection");
        return false;
    }
    return qc->InitiateMigration();
}

MigrationResult IConnection::InitiateMigrationTo(const std::string& local_ip, uint16_t local_port) {
    auto qc = quic_connection_.lock();
    if (!qc) {
        LOG_WARN("IConnection::InitiateMigrationTo: no QUIC connection");
        return MigrationResult::kFailedInvalidState;
    }
    return qc->InitiateMigrationTo(local_ip, local_port);
}

void IConnection::SetMigrationCallback(migration_callback cb) {
    auto qc = quic_connection_.lock();
    if (qc) {
        qc->SetMigrationCallback(cb);
    }
}

bool IConnection::IsMigrationSupported() const {
    auto qc = quic_connection_.lock();
    if (!qc) {
        return false;
    }
    return qc->IsMigrationSupported();
}

bool IConnection::IsMigrationInProgress() const {
    auto qc = quic_connection_.lock();
    if (!qc) {
        return false;
    }
    return qc->IsMigrationInProgress();
}

void IConnection::HandleSettings(const std::unordered_map<uint64_t, uint64_t>& settings) {
    // RFC 9114 Section 4.1: Mark SETTINGS as received
    settings_received_ = true;

    // Store the PEER's settings in their own map (P1-H3): merging them into
    // the map holding our own advertised values used to make "peer omitted a
    // setting" indistinguishable from "peer advertised our value".
    for (auto iter = settings.begin(); iter != settings.end(); ++iter) {
        // RFC 9114 §7.2.4.1: IDs 0x02, 0x03, 0x04, 0x05 are reserved (HTTP/2 legacy).
        // Receipt of these MUST be treated as a connection error of type H3_SETTINGS_ERROR.
        // (The frame decoder rejects them with full-width ids already; this is
        // defence in depth for older/other frame paths.)
        uint64_t id = iter->first;
        if (id >= 0x02 && id <= 0x05) {
            LOG_ERROR("received forbidden HTTP/2 settings id: 0x%llx", (unsigned long long)id);
            Close(0x109);  // H3_SETTINGS_ERROR
            return;
        }
        // RFC 9114 §7.2.4: Store peer's setting value directly (not min).
        peer_settings_[id] = iter->second;
        LOG_DEBUG("peer setting. key:%llu, value:%llu", (unsigned long long)id,
            (unsigned long long)peer_settings_[id]);
    }

    // RFC 9204 §3.2.3: After receiving peer's SETTINGS, cap our encoder's dynamic
    // table capacity to peer's advertised QPACK_MAX_TABLE_CAPACITY.
    // Our encoder MUST NOT use a capacity larger than what the peer allows.
    // If the peer OMITS the setting its capacity is 0 (RFC 9204 default) —
    // do NOT fall back to our local value (P1-H3).
    //
    // NOTE: SETTINGS may arrive *before* the local Init() finishes wiring its
    // own configured cap (the peer's control SETTINGS frame is delivered to
    // us synchronously inside our outbound SendSettings call in some test
    // harnesses). The encoder maintains separate local/peer cap fields and
    // recomputes min(local, peer) on each setter, so the order of arrival
    // does not matter — both sides converge on the same cap.
    auto it = peer_settings_.find(0x01);  // SETTINGS_QPACK_MAX_TABLE_CAPACITY = 0x01
    if (it != peer_settings_.end()) {
        uint32_t peer_cap = static_cast<uint32_t>(it->second);
        qpack_encoder_->SetPeerMaxTableCapacity(peer_cap);
        LOG_DEBUG("HandleSettings: peer qpack_max_table_capacity=%u, encoder cap set to %u", peer_cap,
            qpack_encoder_->GetMaxTableCapacity());
    }

    // P1-H4a (RFC 9204 §4.3.1): once the peer's SETTINGS are known, the
    // encoder MUST send Set Dynamic Table Capacity before inserting entries
    // — the peer's decoder table starts at 0, so without this instruction
    // every referenced insert would be undecodable on the other side. Send
    // min(local, peer) (what GetMaxTableCapacity() now computes) whenever
    // the negotiated capacity is non-zero. SETTINGS arrives exactly once
    // (duplicate SETTINGS is a connection error, see the control stream), so
    // this cannot double-send.
    uint64_t negotiated_cap = qpack_encoder_->GetMaxTableCapacity();
    if (negotiated_cap > 0 && qpack_encoder_sender_) {
        LOG_INFO("HandleSettings: sending Set Dynamic Table Capacity=%llu on QPACK encoder stream",
            (unsigned long long)negotiated_cap);
        qpack_encoder_sender_->SendSetCapacity(negotiated_cap);
    }
}

std::function<void(uint64_t, uint32_t)> IConnection::MakeErrorHandler() {
    std::weak_ptr<IConnection> weak_self = weak_from_this();
    return [weak_self](uint64_t stream_id, uint32_t error_code) {
        auto self = weak_self.lock();
        if (!self) {
            return;
        }
        // Error handlers are also used by UnidentifiedStream to signal
        // terminal release (error_code 0 on FIN). Drop the unidentified
        // bookkeeping before dispatching.
        self->ReleaseUnidentifiedStream(stream_id);
        self->DispatchH3Error(stream_id, error_code);
    };
}

void IConnection::DispatchH3Error(uint64_t stream_id, uint32_t error_code) {
    // Normal completion (error 0, e.g. UnidentifiedStream FIN release): no
    // on-wire signalling, straight to the per-role cleanup path.
    if (error_code == 0) {
        HandleError(stream_id, 0);
        return;
    }

    // Classify by stream kind: errors raised on critical streams are
    // connection errors regardless of the code (RFC 9114 §6.2.1/§6.2.3).
    bool critical_stream = false;
    auto it = streams_.find(stream_id);
    if (it != streams_.end() && it->second) {
        switch (it->second->GetType()) {
            case StreamType::kControl:
            case StreamType::kQpackEncoder:
            case StreamType::kQpackDecoder:
            case StreamType::kUnidentified:
                critical_stream = true;
                break;
            default:
                break;
        }
    }

    // What goes on the wire. A code that is not one of ours (a QUIC transport
    // code, or the value the peer chose in its own RESET_STREAM / STOP_SENDING)
    // used to fall into GetErrorScope's default and be echoed straight back as
    // our RESET_STREAM / STOP_SENDING / CONNECTION_CLOSE: a peer-controlled
    // 32-bit value reflected to the peer. Replace it with a fixed code that
    // says what actually happened -- a dead critical stream is
    // H3_CLOSED_CRITICAL_STREAM (RFC 9114 6.2.1), an aborted request is
    // H3_REQUEST_CANCELLED. The ORIGINAL code still reaches the local cleanup /
    // user notification below, where it is useful for diagnosis and harmless.
    uint32_t wire_code = error_code;
    if (!IsKnownHttp3ErrorCode(error_code)) {
        wire_code = critical_stream ? static_cast<uint32_t>(Http3ErrorCode::kClosedCriticalStream)
                                    : static_cast<uint32_t>(Http3ErrorCode::kRequestCancelled);
        LOG_WARN("IConnection: non-HTTP/3 error code 0x%x on stream %llu, sending 0x%x on the wire instead", error_code,
            (unsigned long long)stream_id, wire_code);
    }

    const Http3ErrorScope scope = GetErrorScope(static_cast<Http3ErrorCode>(wire_code));

    if (critical_stream || scope == Http3ErrorScope::kConnection) {
        // Connection-level: QUIC CONNECTION_CLOSE carrying the H3 code so the
        // peer actually observes the failure. Local stream cleanup happens
        // first (HandleError erases the wrapper) so the connection teardown
        // never races a half-registered stream.
        LOG_ERROR("IConnection: connection-level H3 error 0x%x on stream %llu (critical=%d), closing connection",
            error_code, (unsigned long long)stream_id, critical_stream ? 1 : 0);
        HandleError(stream_id, error_code);
        Close(wire_code);
        return;
    }

    // Request-stream scope (kRequestStream, or kContextual on a bidi stream):
    // reset the transport so the peer sees RESET_STREAM + STOP_SENDING with
    // the H3 code instead of waiting forever for a response that never
    // comes. Then run the ordinary local cleanup / user notification.
    LOG_ERROR("IConnection: stream-level H3 error 0x%x on stream %llu, resetting stream transport",
        error_code, (unsigned long long)stream_id);
    if (it != streams_.end() && it->second) {
        it->second->ResetTransport(wire_code);
    }
    // P1-H5c (RFC 9204 §4.4.2): the abandoned stream may still have
    // unacknowledged header blocks at the peer's encoder — cancel them so
    // the peer stops tracking acknowledgments for a dead stream.
    if (qpack_decoder_) {
        qpack_decoder_->CancelStreamFeedback(stream_id);
    }
    HandleError(stream_id, error_code);
}

std::function<void(const std::unordered_map<uint64_t, uint64_t>&)> IConnection::MakeSettingsHandler() {
    std::weak_ptr<IConnection> weak_self = weak_from_this();
    return [weak_self](const std::unordered_map<uint64_t, uint64_t>& settings) {
        auto self = weak_self.lock();
        if (!self) {
            return;
        }
        self->HandleSettings(settings);
    };
}

std::function<void(uint64_t)> IConnection::MakeGoawayHandler() {
    std::weak_ptr<IConnection> weak_self = weak_from_this();
    return [weak_self](uint64_t id) {
        auto self = weak_self.lock();
        if (!self) {
            return;
        }
        self->HandleGoaway(id);
    };
}

bool IConnection::HandleStreamCommon(const std::shared_ptr<IQuicStream>& stream, uint32_t error_code) {
    if (error_code != 0) {
        LOG_ERROR("IConnection::HandleStream: stream error: %d", error_code);
        if (stream) {
            streams_.erase(stream->GetStreamID());
        }
        return true;
    }

    // NOTE: the request-stream concurrency limit below must NOT gate
    // unidirectional control/QPACK streams. Those are mandated by HTTP/3
    // (RFC 9114 §6.2) and their count is bounded by the QUIC
    // unidirectional-stream flow control (STREAMS_BLOCKED_UNIDIRECTIONAL),
    // not by the request-stream limit. If we applied max_concurrent_streams_
    // to them, a peer that opens several concurrent requests would push
    // streams_.size() past the limit and cause HandleStream to return early,
    // leaving the peer's QPACK encoder stream (type 0x02) with NO read
    // callback. Its encoder instructions would then sit buffered in the
    // RecvStream forever, the local decoder dynamic table would never be
    // populated, header-block decode would stay blocked, and the connection
    // would hang -> http3 interop failure.
    //
    // P1-H2g (RFC 9114 §5.1.1): the concurrent-open limit applies to REQUEST
    // STREAMS only — the old check counted every entry in streams_
    // (control / QPACK / unidentified wrappers included) and closed the WHOLE
    // connection on overflow, killing perfectly legal traffic. Request
    // streams are the bidi ReqResp wrappers; exceeding the limit now resets
    // just the offending stream (H3_REQUEST_REJECTED, RFC 9114 §8.1) and
    // leaves the connection healthy. (Hard transport enforcement of
    // MAX_STREAMS remains the QUIC layer's job, RFC 9000 §19.6.)
    if (stream->GetDirection() == StreamDirection::kBidi) {
        uint64_t request_streams = 0;
        for (const auto& entry : streams_) {
            if (entry.second && (entry.second->GetType() == StreamType::kReqResp ||
                    entry.second->GetType() == StreamType::kPush)) {
                request_streams++;
            }
        }
        if (request_streams >= max_concurrent_streams_) {
            LOG_ERROR("IConnection::HandleStream: max concurrent REQUEST streams reached (%llu >= %llu), "
                      "resetting new stream %llu with H3_REQUEST_REJECTED",
                (unsigned long long)request_streams, (unsigned long long)max_concurrent_streams_,
                (unsigned long long)stream->GetStreamID());
            auto bidi = std::dynamic_pointer_cast<IQuicBidirectionStream>(stream);
            if (bidi) {
                bidi->Reset(static_cast<uint32_t>(Http3ErrorCode::kRequestRejected));
            } else {
                auto recv = std::dynamic_pointer_cast<IQuicRecvStream>(stream);
                if (recv) {
                    recv->Reset(static_cast<uint32_t>(Http3ErrorCode::kRequestRejected));
                }
            }
            return true;  // handled (rejected): caller must not register it
        }
    }
    return false;
}

void IConnection::AttachUnidentifiedStream(const std::shared_ptr<IQuicStream>& stream) {
    // RFC 9114 §6.2: all unidirectional streams begin with a stream type.
    // Create an UnidentifiedStream to read the type byte first; it dispatches
    // back into OnStreamTypeIdentified() (virtual CreateTypedStream) via a
    // weak_ptr, so a late callback after destruction is a no-op.
    auto recv_stream = std::dynamic_pointer_cast<IQuicRecvStream>(stream);
    std::weak_ptr<IConnection> weak_self = weak_from_this();
    auto unidentified = std::make_shared<UnidentifiedStream>(recv_stream, MakeErrorHandler(),
        [weak_self](uint64_t stream_type, std::shared_ptr<IQuicRecvStream> s, std::shared_ptr<IBufferRead> remaining_data) {
            auto self = weak_self.lock();
            if (!self) {
                return;
            }
            self->OnStreamTypeIdentified(stream_type, s, remaining_data);
        });

    // Cap the number of streams pending type identification. A peer that
    // opens streams but never completes the type varint would otherwise pin
    // one wrapper per stream forever (unbounded memory growth).
    if (unidentified_stream_count_ >= kMaxUnidentifiedStreams) {
        LOG_ERROR("IConnection: too many unidentified streams (%llu), closing connection",
            (unsigned long long)unidentified_stream_count_);
        Close(Http3ErrorCode::kStreamCreationError);
        return;
    }
    unidentified_stream_count_++;

    // Store temporarily until stream type is identified
    streams_[stream->GetStreamID()] = unidentified;
}

void IConnection::ReleaseUnidentifiedStream(uint64_t stream_id) {
    auto it = streams_.find(stream_id);
    if (it == streams_.end()) {
        return;
    }
    if (std::dynamic_pointer_cast<UnidentifiedStream>(it->second)) {
        streams_.erase(it);
        if (unidentified_stream_count_ > 0) {
            unidentified_stream_count_--;
        }
    }
}

void IConnection::OnStreamTypeIdentified(
    uint64_t stream_type, std::shared_ptr<IQuicRecvStream> stream, std::shared_ptr<IBufferRead> remaining_data) {
    LOG_DEBUG("IConnection: stream type %llu identified for stream %llu", stream_type, (unsigned long long)stream->GetStreamID());

    // Remove the temporary UnidentifiedStream
    if (unidentified_stream_count_ > 0) {
        unidentified_stream_count_--;
    }
    streams_.erase(stream->GetStreamID());

    // P1-H2b (RFC 9114 §6.2.1/§6.2.3): the control stream and each QPACK
    // stream may only be opened ONCE per direction. A second arrival is a
    // protocol violation — H3_STREAM_CREATION_ERROR, a connection error.
    switch (stream_type) {
        case static_cast<uint64_t>(StreamType::kControl):
            if (control_stream_received_) {
                LOG_ERROR("IConnection: duplicate control stream (type %llu), H3_STREAM_CREATION_ERROR", stream_type);
                Close(static_cast<uint32_t>(Http3ErrorCode::kStreamCreationError));
                return;
            }
            control_stream_received_ = true;
            break;
        case static_cast<uint64_t>(StreamType::kQpackEncoder):
            if (qpack_encoder_stream_received_) {
                LOG_ERROR("IConnection: duplicate QPACK encoder stream, H3_STREAM_CREATION_ERROR");
                Close(static_cast<uint32_t>(Http3ErrorCode::kStreamCreationError));
                return;
            }
            qpack_encoder_stream_received_ = true;
            break;
        case static_cast<uint64_t>(StreamType::kQpackDecoder):
            if (qpack_decoder_stream_received_) {
                LOG_ERROR("IConnection: duplicate QPACK decoder stream, H3_STREAM_CREATION_ERROR");
                Close(static_cast<uint32_t>(Http3ErrorCode::kStreamCreationError));
                return;
            }
            qpack_decoder_stream_received_ = true;
            break;
        default:
            break;
    }

    auto typed_stream = CreateTypedStream(stream_type, stream);
    if (typed_stream) {
        streams_[stream->GetStreamID()] = typed_stream;

        // Feed remaining data to the new stream if any
        typed_stream->OnData(remaining_data, false, 0);
    } else {
        // P1-H2f (RFC 9114 §6.2.3): an unknown unidirectional stream type
        // MUST be ignored — but "ignored" means we stop reading it and tell
        // the peer to stop sending. STOP_SENDING carries the H3 error code
        // (RecvStream::Reset on the peer-opened half emits exactly that
        // frame) so the peer observes the rejection instead of pumping
        // bytes at a reader that will never drain them (amplification
        // budget burn). ~22 bytes, well inside any remaining budget.
        LOG_WARN("IConnection: unknown stream type %llu on stream %llu — STOP_SENDING + discard",
            (unsigned long long)stream_type, (unsigned long long)stream->GetStreamID());
        stream->Reset(static_cast<uint32_t>(Http3ErrorCode::kStreamCreationError));
    }
}

std::shared_ptr<IRecvStream> IConnection::CreateTypedStream(
    uint64_t stream_type, const std::shared_ptr<IQuicRecvStream>& stream) {
    switch (stream_type) {
        case static_cast<uint64_t>(StreamType::kQpackEncoder):  // QPACK Encoder Stream (RFC 9204 Section 4.2)
            LOG_DEBUG("IConnection: creating QPACK Encoder Receiver Stream for stream %llu", stream->GetStreamID());
            // RFC 9204: Peer's encoder instructions populate our LOCAL decoder table (qpack_decoder_)
            return std::make_shared<QpackEncoderReceiverStream>(stream, qpack_decoder_, blocked_registry_, MakeErrorHandler());

        case static_cast<uint64_t>(StreamType::kQpackDecoder):  // QPACK Decoder Stream (RFC 9204 Section 4.2)
            LOG_DEBUG("IConnection: creating QPACK Decoder Receiver Stream for stream %llu", stream->GetStreamID());
            return std::make_shared<QpackDecoderReceiverStream>(stream, qpack_encoder_, blocked_registry_, MakeErrorHandler());

        default:
            // RFC 9114 Section 6.2: unknown stream types MUST be ignored
            LOG_WARN("IConnection: unknown stream type %llu on stream %llu, ignoring", stream_type, stream->GetStreamID());
            return nullptr;
    }
}

void IConnection::HandleGoaway(uint64_t id) {
    // RFC 9114 §5.2: the GOAWAY id MUST NOT increase across multiple
    // GOAWAYs; a peer that violates this MUST be treated as H3_ID_ERROR.
    if (goaway_received_id_ != kNoGoaway && id > goaway_received_id_) {
        LOG_ERROR("IConnection::HandleGoaway: peer GOAWAY id increased (%llu -> %llu), closing with H3_ID_ERROR",
            (unsigned long long)goaway_received_id_, (unsigned long long)id);
        Close(static_cast<uint32_t>(Http3ErrorCode::kIdError));
        return;
    }
    LOG_INFO("IConnection::HandleGoaway: peer GOAWAY received, id=%llu", (unsigned long long)id);
    goaway_received_id_ = id;
    // Do NOT Close() here: in-flight transfers may still land. The role hook
    // below decides whether more is needed (the server starts its symmetric
    // drain so s2n-quic-style peers don't block on our GOAWAY).
    OnGoawayReceived();
}

bool IConnection::SendGoawayFrame(uint64_t goaway_id) {
    if (!control_sender_stream_) {
        LOG_WARN("IConnection::SendGoawayFrame: no control sender stream");
        return false;
    }
    return control_sender_stream_->SendGoaway(goaway_id);
}

void IConnection::WireReqRespStream(const std::shared_ptr<ReqRespBaseStream>& stream) {
    // RFC 9114 §4.2.2: enforce the SETTINGS_MAX_FIELD_SECTION_SIZE we
    // advertised in our SETTINGS, instead of only announcing it.
    //
    // Read from pending_settings_ (the local Http3Settings whose value we
    // advertised), NOT from the settings_ map: that map also holds the
    // PEER's SETTINGS values (HandleSettings merges them into the same
    // map), and the peer's advertised limit governs what WE may send —
    // not what we accept. Reading the map let a peer that also advertises
    // the setting silently change our inbound header limit (code review
    // follow-up: settings_ local/peer conflation).
    stream->SetMaxFieldSectionSize(pending_settings_.max_field_section_size);

    // Propagate qlog trace from the QUIC connection to the HTTP/3 stream.
    auto qlog_trace = GetQuicQlogTrace();
    if (qlog_trace) {
        stream->SetQlogTrace(qlog_trace);
    }
}

const std::unordered_map<uint64_t, uint64_t> IConnection::AdaptSettings(const Http3Settings& settings) {
    std::unordered_map<uint64_t, uint64_t> settings_map;
    // RFC 9114 §7.2.4.1: Only valid HTTP/3 settings may be sent on the wire.
    // IDs 0x02-0x05 are forbidden (HTTP/2 legacy) and MUST NOT be sent.
    settings_map[SettingsType::kMaxFieldSectionSize] = settings.max_field_section_size;
    settings_map[SettingsType::kQpackMaxTableCapacity] = settings.qpack_max_table_capacity;
    settings_map[SettingsType::kQpackBlockedStreams] = settings.qpack_blocked_streams;
    settings_map[SettingsType::kEnableConnectProtocol] = 0;

    return settings_map;
}

void IConnection::StartCleanupTimer() {
    if (cleanup_timer_id_ != 0) {
        // Already started (id is minted on first call); a periodic wheel timer
        // re-arms itself, so there is nothing to do here.
        return;
    }
    auto qc = quic_connection_.lock();
    if (!qc) {
        return;
    }

    // Capture weak_ptr to this to safely handle timer callbacks after object destruction
    std::weak_ptr<IConnection> weak_self = weak_from_this();

    auto cb = [weak_self]() {
        // Check if the connection relies alive
        auto self = weak_self.lock();
        if (!self) {
            // Connection destroyed, do nothing
            return;
        }

        self->CleanupDestroyedStreams();
    };

    // One periodic wheel timer replaces the old one-shot + self-rescheduling
    // pattern. This skips re-allocating a slab entry and copying the closure on
    // every tick, and removes the re-arm path that used to refill the draining
    // slot whenever the wheel lagged real time. The timer cancels itself when
    // the owning QUIC connection (and thus its TimerCoordinator life token)
    // is destroyed.
    cleanup_timer_id_ = qc->AddTimer(std::move(cb), kStreamCleanupIntervalMs,
        /*periodic=*/true);
}

void IConnection::CleanupDestroyedStreams() {
    if (!streams_to_destroy_.empty()) {
        LOG_DEBUG(
            "IConnection::CleanupDestroyedStreams: cleaning up %zu completed streams", streams_to_destroy_.size());
        streams_to_destroy_.clear();
    }

    // RFC 9114 §5.2 graceful drain probe: once we've sent GOAWAY and the
    // last request/push stream has finished, emit CONNECTION_CLOSE
    // (H3_NO_ERROR=0). Done from the cleanup timer rather than HandleError
    // to avoid racing with ScheduleStreamRemoval, which keeps the just-
    // finished stream alive in streams_to_destroy_ for one extra tick;
    // by the time we land here that holding-area has been cleared above,
    // so HasInFlightRequests() reflects the real state.
    auto qc = quic_connection_.lock();
    if (draining_ && qc && !qc->IsTerminating() && !HasInFlightRequests()) {
        LOG_INFO("IConnection::CleanupDestroyedStreams: drain complete, emitting CONNECTION_CLOSE(H3_NO_ERROR)");
        Close(0);
    }
}

void IConnection::ScheduleStreamRemoval(uint64_t stream_id) {
    // Move stream from active map to holding area
    // This removes it from streams_ (so it won't count against limits)
    // but keeps the shared_ptr alive temporarily to prevent use-after-free
    auto iter = streams_.find(stream_id);
    if (iter != streams_.end()) {
        LOG_DEBUG("IConnection::ScheduleStreamRemoval: moving stream %llu to holding area", stream_id);

        // Move to holding area - this keeps the object alive until next cleanup cycle
        streams_to_destroy_.push_back(iter->second);

        // Remove from active streams map immediately
        streams_.erase(iter);
    }
}

}  // namespace http3
}  // namespace quicx
