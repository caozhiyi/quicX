#ifndef HTTP3_CONNECTION_IF_CONNECTION
#define HTTP3_CONNECTION_IF_CONNECTION

#include <atomic>
#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>

#include <quicx/http3/type.h>
#include "http3/qpack/blocked_registry.h"
#include "http3/qpack/qpack_encoder.h"
#include "http3/stream/if_stream.h"
#include <quicx/quic/if_quic_connection.h>

namespace quicx {
namespace http3 {

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
     * @param quic_connection The QUIC connection
     * @param error_handler The error handler
     */
    IConnection(const std::string& unique_id, const std::shared_ptr<IQuicConnection>& quic_connection,
        const std::function<void(const std::string& unique_id, uint32_t error_code)>& error_handler);
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
     *        The returned shared_ptr aliases the same IQuicConnection that was
     *        passed at construction time and MUST NOT be retained beyond the
     *        immediate check — holding it would extend the per-connection
     *        memory footprint that P4 is trying to reclaim.
     */
    const std::shared_ptr<IQuicConnection>& GetQuicConnection() const { return quic_connection_; }

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
     * @brief Subclass hook: emit a GOAWAY frame on the local control stream.
     *
     * Server connections write the largest stream ID they will process
     * (RFC 9114 §5.2). Client connections write the largest push ID they
     * will accept. Returning false tells Shutdown() that the GOAWAY could
     * not be sent (e.g., control sender stream gone) and the drain still
     * proceeds — peer-side state will catch up via QUIC CONNECTION_CLOSE.
     *
     * Implementations MUST be idempotent for repeated calls with the same
     * id; the base class enforces "id must not increase".
     */
    virtual bool SendGoawayFrame(uint64_t goaway_id) = 0;

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

protected:
    // Schedule stream removal - moves stream to holding area to delay destruction
    void ScheduleStreamRemoval(uint64_t stream_id);

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

    std::shared_ptr<IQuicConnection> quic_connection_;

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

#endif
