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
    qpack_decoder_ = std::make_shared<QpackEncoder>();
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

    settings_ = AdaptSettings(settings);
    control_sender_stream_->SendSettings(settings_);

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

void IConnection::HandleSettings(const std::unordered_map<uint16_t, uint64_t>& settings) {
    // RFC 9114 Section 4.1: Mark SETTINGS as received
    settings_received_ = true;

    // merge settings
    for (auto iter = settings.begin(); iter != settings.end(); ++iter) {
        // RFC 9114 §7.2.4.1: IDs 0x02, 0x03, 0x04, 0x05 are reserved (HTTP/2 legacy).
        // Receipt of these MUST be treated as a connection error of type H3_SETTINGS_ERROR.
        uint16_t id = iter->first;
        if (id == 0x02 || id == 0x03 || id == 0x04 || id == 0x05) {
            LOG_ERROR("received forbidden HTTP/2 settings id: 0x%02x", id);
            Close(0x109);  // H3_SETTINGS_ERROR
            return;
        }
        // RFC 9114 §7.2.4: Store peer's setting value directly (not min).
        settings_[iter->first] = iter->second;
        LOG_DEBUG("settings. key:%d, value:%d", iter->first, settings_[iter->first]);
    }

    // RFC 9204 §3.2.3: After receiving peer's SETTINGS, cap our encoder's dynamic
    // table capacity to peer's advertised QPACK_MAX_TABLE_CAPACITY.
    // Our encoder MUST NOT use a capacity larger than what the peer allows.
    //
    // NOTE: SETTINGS may arrive *before* the local Init() finishes wiring its
    // own configured cap (the peer's control SETTINGS frame is delivered to
    // us synchronously inside our outbound SendSettings call in some test
    // harnesses). The encoder maintains separate local/peer cap fields and
    // recomputes min(local, peer) on each setter, so the order of arrival
    // does not matter — both sides converge on the same cap.
    auto it = settings_.find(0x01);  // SETTINGS_QPACK_MAX_TABLE_CAPACITY = 0x01
    if (it != settings_.end()) {
        uint32_t peer_cap = static_cast<uint32_t>(it->second);
        qpack_encoder_->SetPeerMaxTableCapacity(peer_cap);
        LOG_DEBUG("HandleSettings: peer qpack_max_table_capacity=%u, encoder cap set to %u", peer_cap,
            qpack_encoder_->GetMaxTableCapacity());
    }
}

std::function<void(uint64_t, uint32_t)> IConnection::MakeErrorHandler() {
    std::weak_ptr<IConnection> weak_self = weak_from_this();
    return [weak_self](uint64_t stream_id, uint32_t error_code) {
        auto self = weak_self.lock();
        if (!self) {
            return;
        }
        self->HandleError(stream_id, error_code);
    };
}

std::function<void(const std::unordered_map<uint16_t, uint64_t>&)> IConnection::MakeSettingsHandler() {
    std::weak_ptr<IConnection> weak_self = weak_from_this();
    return [weak_self](const std::unordered_map<uint16_t, uint64_t>& settings) {
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
    if (stream->GetDirection() == StreamDirection::kBidi && streams_.size() >= max_concurrent_streams_) {
        LOG_ERROR("IConnection::HandleStream: max concurrent streams reached");
        Close(Http3ErrorCode::kStreamCreationError);
        return true;
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

    // Store temporarily until stream type is identified
    streams_[stream->GetStreamID()] = unidentified;
}

void IConnection::OnStreamTypeIdentified(
    uint64_t stream_type, std::shared_ptr<IQuicRecvStream> stream, std::shared_ptr<IBufferRead> remaining_data) {
    LOG_DEBUG("IConnection: stream type %llu identified for stream %llu", stream_type, stream->GetStreamID());

    // Remove the temporary UnidentifiedStream
    streams_.erase(stream->GetStreamID());

    auto typed_stream = CreateTypedStream(stream_type, stream);
    if (typed_stream) {
        streams_[stream->GetStreamID()] = typed_stream;

        // Feed remaining data to the new stream if any
        typed_stream->OnData(remaining_data, false, 0);
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
    auto it = settings_.find(static_cast<uint16_t>(SettingsType::kMaxFieldSectionSize));
    if (it != settings_.end()) {
        stream->SetMaxFieldSectionSize(it->second);
    }

    // Propagate qlog trace from the QUIC connection to the HTTP/3 stream.
    auto qlog_trace = GetQuicQlogTrace();
    if (qlog_trace) {
        stream->SetQlogTrace(qlog_trace);
    }
}

const std::unordered_map<uint16_t, uint64_t> IConnection::AdaptSettings(const Http3Settings& settings) {
    std::unordered_map<uint16_t, uint64_t> settings_map;
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
