#ifndef HTTP3_CONNECTION_IF_CONNECTION
#define HTTP3_CONNECTION_IF_CONNECTION

#include <atomic>
#include <functional>
#include <memory>
#include <quicx/http3/type.h>
#include <quicx/quic/if_quic_connection.h>
#include <quicx/quic/if_quic_stream.h>
#include <unordered_map>
#include <vector>

#include "quic/connection/if_connection.h"

#include "http3/qpack/blocked_registry.h"
#include "http3/qpack/qpack_encoder.h"
#include "http3/stream/control_client_sender_stream.h"
#include "http3/stream/if_recv_stream.h"
#include "http3/stream/if_stream.h"

namespace quicx {
namespace http3 {

class ReqRespBaseStream;

/**
 * @brief IConnection is the base class for all HTTP/3 connections
 *
 * This class is used to manage the HTTP/3 connection.
 */
class IConnection: public std::enable_shared_from_this<IConnection> {
public:
    /**
     * @brief Constructor
     * @param unique_id The unique id of the connection
     * @param settings HTTP/3 settings to advertise in our SETTINGS frame
     * @param quic_connection The QUIC connection
     * @param error_handler The error handler
     * @param max_concurrent_streams Local cap on request/push streams
     * @param enable_push Whether server push is enabled
     */
    IConnection(const std::string& unique_id, const Http3Settings& settings,
        const std::shared_ptr<IQuicConnection>& quic_connection,
        const std::function<void(const std::string& unique_id, uint32_t error_code)>& error_handler,
        uint64_t max_concurrent_streams, bool enable_push);
    virtual ~IConnection();

    /**
     * @brief Initialize the connection
     */
    virtual void Init();

    /**
     * @brief Get the unique id of the connection
     * @return The unique id of the connection
     */
    const std::string& GetUniqueId() const { return unique_id_; }

    /**
     * @brief Get the underlying QUIC connection (for owner-side identity checks
     *        such as reverse-lookup in Http3 Client/Server connection maps when
     *        a kConnectionClose notification arrives without a routable key).
     *
     * The connection is held weakly on purpose: a ServerConnection / ClientConnection
     * is owned by the QUIC connection itself (via IQuicConnection::SetContext), so a
     * strong back-reference here would form a reference cycle that leaks the
     * connection. Callers MUST NOT retain the returned shared_ptr beyond the
     * immediate check — it may already be expired, and holding it would extend
     * the per-connection memory footprint that P4 is trying to reclaim.
     */
    std::shared_ptr<IQuicConnection> GetQuicConnection() const { return quic_connection_.lock(); }

    /**
     * @brief Get the qlog trace owned by the underlying QUIC connection.
     *
     * The qlog trace is an internal type (common::QlogTrace) that is
     * deliberately not exposed on the public IQuicConnection interface;
     * it is reachable only via the internal quic::IConnection interface.
     * Returns nullptr if the QUIC connection is gone, is not a production
     * quic::IConnection (e.g. a test mock), or qlog is disabled.
     */
    std::shared_ptr<common::QlogTrace> GetQuicQlogTrace() const {
        auto quic_conn = std::dynamic_pointer_cast<quic::IConnection>(quic_connection_.lock());
        return quic_conn ? quic_conn->GetQlogTrace() : nullptr;
    }

    /**
     * @brief Close the connection
     * @param error_code The error code
     */
    virtual void Close(uint32_t error_code);

    /**
     * @brief Initiate a graceful shutdown (RFC 9114 §5.2).
     *
     * Sends a GOAWAY frame on the local control stream and enters the
     * "graceful drain" state. While draining:
     *   - new requests/pushes initiated locally are refused
     *     (IsAcceptingNewRequests() / IsAcceptingNewPushes() return false);
     *   - already in-flight request/response streams are allowed to
     *     finish — the cleanup timer polls every 100ms and once no
     *     request/push streams remain, calls Close(0) to emit
     *     CONNECTION_CLOSE(H3_NO_ERROR).
     *
     * Calling Shutdown() twice with a smaller id is a no-op (RFC 9114
     * §5.2: GOAWAY id MUST NOT increase). Calling it after Close() is a
     * no-op.
     */
    virtual void Shutdown();

    /**
     * @brief Whether the connection is still accepting locally-initiated
     *        requests. Returns false once we've sent or received a GOAWAY.
     */
    bool IsAcceptingNewRequests() const;

    /**
     * @brief Whether the connection is still accepting server pushes.
     *        Returns false once a GOAWAY is in flight in either direction.
     */
    bool IsAcceptingNewPushes() const;

    /**
     * @brief Initiate connection migration (simple API for interop tests)
     * @return True if migration was initiated successfully
     */
    virtual bool InitiateMigration();

    /**
     * @brief Initiate connection migration to a specific local address (production API)
     * @param local_ip New local IP address
     * @param local_port New local port (0 = system chooses)
     * @return MigrationResult indicating success or failure
     */
    virtual MigrationResult InitiateMigrationTo(const std::string& local_ip, uint16_t local_port = 0);

    /**
     * @brief Set callback for migration events
     * @param cb Callback to invoke on migration events
     */
    virtual void SetMigrationCallback(migration_callback cb);

    /**
     * @brief Check if active migration is supported by peer
     * @return True if migration is supported
     */
    virtual bool IsMigrationSupported() const;

    /**
     * @brief Check if migration is currently in progress
     * @return True if migration is in progress
     */
    virtual bool IsMigrationInProgress() const;

protected:
    // handle stream
    virtual void HandleStream(std::shared_ptr<IQuicStream> stream, uint32_t error_code) = 0;
    // handle error
    virtual void HandleError(uint64_t stream_id, uint32_t error_code) = 0;
    // handle settings
    virtual void HandleSettings(const std::unordered_map<uint16_t, uint64_t>& settings);

    /**
     * @brief Common HandleStream() prefix shared by client and server.
     *
     * Handles the two paths that are identical on both roles:
     *   1. error != 0  -> drop the stream from streams_ (transport reports
     *      the stream is gone; nothing role-specific to do);
     *   2. bidirectional stream while at max_concurrent_streams_ ->
     *      Close(H3_STREAM_CREATION_ERROR). Unidirectional control/QPACK
     *      streams are deliberately exempt (RFC 9114 §6.2 mandates them and
     *      their count is bounded by QUIC uni flow control, not by the
     *      request-stream cap).
     *
     * @return true when the stream was fully handled by common logic and
     *         the subclass HandleStream() must return immediately.
     */
    bool HandleStreamCommon(const std::shared_ptr<IQuicStream>& stream, uint32_t error_code);

    /**
     * @brief Wrap a newly accepted unidirectional stream in an
     *        UnidentifiedStream and register it until its type byte arrives
     *        (RFC 9114 §6.2). The type callback dispatches back into
     *        OnStreamTypeIdentified() through a weak_ptr, so subclasses get
     *        correct virtual dispatch without owning this wiring.
     */
    void AttachUnidentifiedStream(const std::shared_ptr<IQuicStream>& stream);

    /**
     * @brief Non-virtual skeleton for RFC 9114 §6.2 stream typing: drop the
     *        temporary UnidentifiedStream, ask CreateTypedStream() for the
     *        role-specific stream, register it and replay any data that was
     *        buffered while the type byte was pending.
     */
    void OnStreamTypeIdentified(
        uint64_t stream_type, std::shared_ptr<IQuicRecvStream> stream, std::shared_ptr<IBufferRead> remaining_data);

    /**
     * @brief Role-specific stream factory. The base class handles the two
     *        QPACK receiver streams (identical on both roles) and the
     *        "unknown stream type -> ignore" rule (RFC 9114 §6.2).
     *        Subclasses override to add kControl / kPush and fall through
     *        to the base for everything else.
     *
     * @return the typed stream, or nullptr to drop it (unknown type or
     *         role-specific protocol violation).
     */
    virtual std::shared_ptr<IRecvStream> CreateTypedStream(
        uint64_t stream_type, const std::shared_ptr<IQuicRecvStream>& stream);

    /**
     * @brief Non-virtual GOAWAY ingestion shared by both roles: enforce the
     *        RFC 9114 §5.2 "id MUST NOT increase" rule (violation closes
     *        with H3_ID_ERROR) and record goaway_received_id_. Afterwards,
     *        calls OnGoawayReceived() so roles can react (the server starts
     *        its symmetric drain).
     */
    void HandleGoaway(uint64_t id);

    /**
     * @brief Role hook invoked after a valid peer GOAWAY has been recorded.
     *        Default: no further action (client behaviour — in-flight
     *        responses may still land).
     */
    virtual void OnGoawayReceived() {}

    /**
     * @brief Subclass hook: emit a GOAWAY frame on the local control stream.
     *
     * Both roles send GOAWAY over the same ControlClientSenderStream, so the
     * implementation lives here; only ComputeGoawayId() is role-specific.
     * Returning false tells Shutdown() that the GOAWAY could not be sent
     * (e.g. control sender stream gone) and the drain still proceeds —
     * peer-side state will catch up via QUIC CONNECTION_CLOSE.
     *
     * Implementations MUST be idempotent for repeated calls with the same
     * id; the base class enforces "id must not increase".
     */
    bool SendGoawayFrame(uint64_t goaway_id);

    /**
     * @brief Subclass hook: compute the GOAWAY id at the moment Shutdown()
     *        is invoked. Servers return next-stream-id-they-WON'T-process
     *        (typically max(seen_request_stream_id) + 4); clients return
     *        max push_id they have accepted (typically next_push_id_).
     */
    virtual uint64_t ComputeGoawayId() = 0;

    /**
     * @brief Subclass hook: are there still in-flight HTTP/3 request or
     *        push streams that block a graceful close? Long-lived control
     *        and QPACK streams MUST NOT count.
     */
    virtual bool HasInFlightRequests() const;

    static const std::unordered_map<uint16_t, uint64_t> AdaptSettings(const Http3Settings& settings);

    /**
     * @brief Check if peer SETTINGS has been received
     * @return True if SETTINGS received, false otherwise
     */
    bool SettingsReceived() const { return settings_received_; }

    // ------------------------------------------------------------------
    // Weak-self helpers for stream/timer callbacks.
    //
    // Per docs/zh/design/ownership_and_memory.md (§2.2, §3.1, §5):
    // upward references from streams to connection MUST be weak_ptr, and
    // short-lived event callbacks MUST do weak_from_this() + lock().
    //
    // Streams are owned by IConnection::streams_, but their handler closures
    // can outlive the immediate call frame (queued via QUIC layer / timers).
    // Capturing raw |this| in std::bind led to __cxa_pure_virtual / SIGABRT
    // when a callback fired against a half-destroyed connection. Capturing
    // shared_from_this() instead is also wrong: it would form a self-cycle
    // (Connection -> streams_ -> Stream -> handler -> shared_ptr<Connection>),
    // pinning the connection forever. weak_ptr breaks both problems.
    // ------------------------------------------------------------------

    // Get a weak_ptr to *this* downcast to the concrete subclass type T.
    // Use inside subclass callsites that need to forward to subclass-private
    // methods, e.g.:
    //   auto weak_self = WeakSelfAs<ClientConnection>();
    //   stream_cb = [weak_self](uint64_t id) {
    //       if (auto self = weak_self.lock()) self->HandlePushPromise(id);
    //   };
    template <typename T>
    std::weak_ptr<T> WeakSelfAs() {
        return std::weak_ptr<T>(std::static_pointer_cast<T>(shared_from_this()));
    }

    // Build a stream-error handler that forwards to IConnection::HandleError
    // through a weak_ptr<IConnection>. Safe to bind into stream callbacks.
    std::function<void(uint64_t, uint32_t)> MakeErrorHandler();

    // Build a settings handler that forwards to IConnection::HandleSettings
    // through a weak_ptr<IConnection>.
    std::function<void(const std::unordered_map<uint16_t, uint64_t>&)> MakeSettingsHandler();

    // Build a GOAWAY handler that forwards to IConnection::HandleGoaway
    // through a weak_ptr<IConnection>.
    std::function<void(uint64_t)> MakeGoawayHandler();

    /**
     * @brief Finish wiring a freshly created request/response stream:
     *        enforce the SETTINGS_MAX_FIELD_SECTION_SIZE we advertised
     *        (RFC 9114 §4.2.2) and propagate the qlog trace from the QUIC
     *        connection. Identical on both roles.
     */
    void WireReqRespStream(const std::shared_ptr<ReqRespBaseStream>& stream);

protected:
    // Schedule stream removal - moves stream to holding area to delay destruction
    void ScheduleStreamRemoval(uint64_t stream_id);

    // SetupControlAndQpackStreams(): the role-agnostic half of Init().
    // Creates our outbound control stream and sends SETTINGS (RFC 9114
    // §6.2.1), configures the QPACK dynamic tables from pending_settings_,
    // and creates + wires the QPACK encoder/decoder sender streams (RFC 9204
    // §6.2: the streams themselves are mandatory even when the dynamic
    // table capacity is 0). Called from Init(); subclasses do their
    // role-specific extras (e.g. the client's initial MAX_PUSH_ID) after
    // calling the base Init().
    void SetupControlAndQpackStreams();

private:
    // Start periodic cleanup timer for destroyed streams
    void StartCleanupTimer();
    // Clean up destroyed streams (called by timer)
    void CleanupDestroyedStreams();

protected:
    // indicate the unique id of the connection
    std::string unique_id_;
    std::shared_ptr<QpackBlockedRegistry> blocked_registry_;
    std::function<void(const std::string& unique_id, uint32_t error_code)> error_handler_;
    std::unordered_map<uint16_t, uint64_t> settings_;
    std::unordered_map<uint64_t, std::shared_ptr<IStream>> streams_;

    // Our outbound HTTP/3 control stream. Same type on both roles (the
    // server also uses ControlClientSenderStream — it sends SETTINGS and
    // GOAWAY the same way), hence owned here rather than per-subclass.
    std::shared_ptr<ControlClientSenderStream> control_sender_stream_;

    // Settings captured at construction, applied during Init() (see the
    // two-phase-init note on Init()).
    Http3Settings pending_settings_;

    // RFC 9204: Two independent QPACK contexts are required per connection.
    // qpack_encoder_ holds the local encoder dynamic table: used when encoding
    // outgoing headers (Encode) and receives Section Ack / Insert Count Increment
    // feedback from the peer's decoder.
    std::shared_ptr<QpackEncoder> qpack_encoder_;

    // qpack_decoder_ holds the local decoder dynamic table: populated by the
    // peer's encoder instructions (Insert With Name Ref, Insert Without Name Ref,
    // Duplicate, Set Capacity) received on the QPACK encoder receiver stream,
    // and used when decoding incoming HEADERS blocks (Decode).
    std::shared_ptr<QpackEncoder> qpack_decoder_;

    // Held weakly on purpose: the QUIC connection owns this IConnection via
    // IQuicConnection::SetContext, so a strong back-reference would form a
    // reference cycle (see GetQuicConnection). Lock before use; may be expired.
    std::weak_ptr<IQuicConnection> quic_connection_;

    // RFC 9114 Section 4.1: Track if peer SETTINGS frame has been received
    bool settings_received_ = false;

    // Local-only connection limits (from Http3Config, not sent in SETTINGS frame)
    uint64_t max_concurrent_streams_ = 200;
    bool enable_push_ = false;

    // Temporary holding area for completed streams to delay destruction
    // This prevents use-after-free when error_handler_ is called from within stream callbacks
    std::vector<std::shared_ptr<IStream>> streams_to_destroy_;

    // Timer ID for periodic cleanup of destroyed streams
    uint64_t cleanup_timer_id_ = 0;

    // Flag to indicate if the connection is being destroyed
    // This is checked by timer callbacks to avoid accessing destroyed objects
    std::shared_ptr<std::atomic<bool>> is_destroying_;

    // ------------------------------------------------------------------
    // RFC 9114 §5.2 graceful shutdown (GOAWAY) state.
    //
    // kNoGoaway sentinel = no GOAWAY observed in that direction yet.
    // Once a GOAWAY is sent or received, the corresponding id is the
    // upper bound (inclusive for stream-id semantics on server-side, etc.)
    // for new resource acceptance.
    //
    // draining_ flips true the moment we *send* GOAWAY — it gates new
    // local request creation and arms the cleanup-timer drain probe.
    // ------------------------------------------------------------------
    static constexpr uint64_t kNoGoaway = static_cast<uint64_t>(-1);
    uint64_t goaway_sent_id_ = kNoGoaway;      // id we advertised in our GOAWAY
    uint64_t goaway_received_id_ = kNoGoaway;  // id peer advertised to us
    bool draining_ = false;                    // local-side drain armed
};

}  // namespace http3
}  // namespace quicx

#endif  // HTTP3_CONNECTION_IF_CONNECTION
