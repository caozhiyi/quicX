#include "common/http/url.h"
#include "common/log/log.h"
#include "common/network/address.h"
#include "common/network/io_handle.h"

#include <mutex>
#include <shared_mutex>
#include <vector>

#include "http3/config.h"
#include "http3/http/client.h"
#include "http3/http/error.h"

namespace quicx {

std::unique_ptr<IClient> IClient::Create(const Http3Settings& settings) {
    return std::unique_ptr<IClient>(static_cast<IClient*>(new http3::Client(settings)));
}

namespace http3 {

Client::Client(const Http3Settings& settings):
    settings_(settings) {
    quic_ = IQuicClient::Create(settings.quic_transport_params_);
    quic_->SetConnectionStateCallBack(std::bind(&Client::OnConnection, this, std::placeholders::_1,
        std::placeholders::_2, std::placeholders::_3, std::placeholders::_4));
}

Client::~Client() {
    Close();
    quic_->Join();
    // P4: by the time Join() returns, the master event loop has stopped, so
    // any CONNECTION_CLOSE callback that was scheduled via AddTimer/timer
    // heap will never fire to erase conn_map_ for us. Drop it explicitly so
    // every ClientConnection — and the shared_ptr<IQuicConnection> /
    // BaseConnection it transitively owns — is released before our members
    // are destroyed. Without this, a quick Close()+Destroy() cycle leaves
    // BaseConnection (~120KB) pinned until process exit.
    conn_map_.clear();
    wait_request_map_.clear();
}

bool Client::Init(const Http3ClientConfig& config) {
    // Store config for later use
    config_ = config;

    return quic_->Init(config.quic_config_);
}

// ---------------------------------------------------------------------------
// Thread-safety note for DoRequest()
// ---------------------------------------------------------------------------
// DoRequest() is called from the application's own (non-event-loop) thread
// and must not directly mutate state that the QUIC master event loop also
// touches: wait_request_map_, pending_close_count_, destroy_scheduled_,
// is_closing_. Touching those containers from the user thread would race
// the event-loop reading/writing them and corrupt their internal state
// (queue front/pop interleaving with push, hash-table rehash mid-read, …).
//
// The hot path (an existing HTTP/3 connection to `host` already exists in
// conn_map_) is the dominant case under steady-state load: e.g. a load
// generator pinning N callers to the same origin will, after the first
// request, hit conn_map_ for every subsequent request. Forcing every one of
// those calls onto the loop via AddTimer(0,...) costs:
//   * a tasks_mu_ lock + a pipe wakeup byte (PostTask),
//   * an epoll/kqueue wake-up of the loop thread,
//   * a timer-wheel insert + drive-out (AddTimer(0)),
//   * an extra cross-thread cache miss on conn_map_.
// In a 50-client × 500-request micro-benchmark this collapsed throughput
// from ~1k+ req/s down to ~180 req/s.
//
// We therefore split the work:
//   1. URL parsing and IRequest mutation stay on the user thread (request
//      is caller-owned and must not be shared concurrently with this call).
//   2. conn_map_ is read on the user thread under a shared_mutex
//      (conn_map_mu_). If we hit, we hand the request straight to the
//      ClientConnection — its DoRequest path is already loop-safe (the
//      underlying SendStream::Send hops onto the loop via RunInLoop when
//      called from a non-loop thread). No PostTask needed.
//   3. On miss we hop onto the QUIC master event loop via
//      quic_->AddTimer(0,...) — which calls master_event_loop_->RunInLoop
//      internally — to do DNS resolution and mutate wait_request_map_ /
//      kick off quic_->Connection. RunInLoop short-circuits to a
//      synchronous dispatch when the caller is already on the loop thread,
//      so re-entry from a callback remains correct.
//
// Errors discovered after the hop (DNS failure, etc.) are reported via the
// supplied handler with the same error_code conventions as OnConnection's
// failure path so the caller still observes a single completion event.
//
// Lifetime of the captured `this` in the dispatched lambdas:
//   ~Client() runs `Close(); quic_->Join();` *before* any member is
//   destroyed. Close() posts its own lambda onto the loop, then Join()
//   blocks until the master event loop has drained and stopped. By the time
//   Join() returns, every AddTimer(0, ...) lambda we ever posted has either
//   (a) already executed to completion on the loop thread, or (b) been
//   discarded by the loop's ClearAllTimers() teardown. In neither case can
//   a lambda observe a half-destroyed Client — so the bare `[this]` capture
//   is safe under the documented API contract: ~Client() must not race a
//   concurrent DoRequest()/Close() call from another thread (the same
//   contract every IClient member function relies on; we do not promise
//   thread-safe destruction). Switching to a weak_ptr<Client> would not
//   help here because `this->quic_` itself is a Client member and would
//   already be gone by the time the lambda dereferenced it.
// ---------------------------------------------------------------------------

namespace {

// Synchronous error report helper, dispatched when the slow-path discovers
// a fatal error after we have already returned `true` to the user.
inline void ReportRequestError(const http_response_handler& handler, uint32_t error_code) {
    if (handler) handler(nullptr, error_code);
}
inline void ReportRequestError(const std::shared_ptr<IAsyncClientHandler>& handler, uint32_t error_code) {
    if (handler) handler->OnError(error_code);
}

}  // namespace

template <typename Handler>
bool Client::DoRequestImpl(const std::string& url, HttpMethod method,
    std::shared_ptr<IRequest> request, Handler handler) {
    std::string scheme, host, path_with_query;
    uint16_t port = 0;
    if (!common::ParseURLForPseudoHeaders(url, scheme, host, port, path_with_query)) {
        LOG_ERROR("parse url failed. url: %s", url.c_str());
        return false;
    }

    // Set pseudo-headers (the IRequest is caller-owned; the caller must not
    // share it across threads concurrently with this call, so it is safe to
    // mutate here on the user thread).
    request->SetMethod(method);
    request->SetScheme(scheme);
    request->SetAuthority(common::BuildAuthority(host, port, scheme));
    request->SetPath(path_with_query);

    // Fast path: an existing HTTP/3 connection already exists for this
    // host. Read conn_map_ under a shared lock, then drop the lock before
    // dispatching the request so we never hold it across user code.
    {
        std::shared_lock<std::shared_mutex> rlock(conn_map_mu_);
        auto it = conn_map_.find(host);
        if (it != conn_map_.end()) {
            auto conn = it->second;
            rlock.unlock();
            conn->DoRequest(request, handler);
            return true;
        }
    }

    // Slow path: no connection yet. Hop onto the QUIC master event loop
    // before touching wait_request_map_ / kicking off quic_->Connection.
    // DNS resolution is also moved here so steady-state hot-path callers
    // never pay for getaddrinfo.
    quic_->AddTimer(0, [this, host, port, request, handler]() {
        // Re-check conn_map_: a concurrent caller may have already
        // established the connection between our user-thread miss and this
        // dispatch.
        {
            std::shared_lock<std::shared_mutex> rlock(conn_map_mu_);
            auto it = conn_map_.find(host);
            if (it != conn_map_.end()) {
                auto conn = it->second;
                rlock.unlock();
                conn->DoRequest(request, handler);
                return;
            }
        }

        common::Address addr;
        if (!common::LookupAddress(host, addr)) {
            LOG_ERROR("lookup address failed. host: %s", host.c_str());
            // Notify the caller asynchronously, mirroring OnConnection's
            // failure path so callers always observe a single completion.
            uint32_t error_code = Http3ErrorCode::kInternalError;
            ReportRequestError(handler, error_code);
            if (error_handler_) {
                error_handler_(host, error_code);
            }
            return;
        }
        addr.SetPort(port);

        std::string addr_key = addr.AsString();
        wait_request_map_[addr_key].push(WaitRequestContext{host, request, handler});

        if (wait_request_map_[addr_key].size() == 1) {
            uint32_t timeout_ms = config_.connection_timeout_ms_;
            quic_->Connection(addr.GetIp(), addr.GetPort(), kHttp3Alpn, timeout_ms, "", host);
        }
    });

    return true;
}

bool Client::DoRequest(const std::string& url, HttpMethod method, std::shared_ptr<IRequest> request,
    const http_response_handler& handler) {
    return DoRequestImpl(url, method, request, handler);
}

bool Client::DoRequest(const std::string& url, HttpMethod method, std::shared_ptr<IRequest> request,
    std::shared_ptr<IAsyncClientHandler> handler) {
    return DoRequestImpl(url, method, request, handler);
}

void Client::OnConnection(
    std::shared_ptr<IQuicConnection> conn, ConnectionOperation operation, uint32_t error, const std::string& reason) {
    // get remote address
    std::string addr;
    uint32_t port;
    conn->GetRemoteAddr(addr, port);

    std::string addr_key = addr + ":" + std::to_string(port);

    if (operation == ConnectionOperation::kConnectionClose) {
        LOG_INFO("connection close. error: %d, reason: %s", error, reason.c_str());

        // P4: Release the HTTP/3 ClientConnection that owns this QUIC conn.
        // Without this erase, conn_map_ (keyed by host) keeps a shared_ptr to
        // ClientConnection alive, which in turn keeps quic_connection_ alive,
        // which prevents ~BaseConnection() from running and leaks ~120KB per
        // connection cycle. We reverse-lookup by comparing the underlying
        // IQuicConnection pointer because the close callback does not carry
        // the host key that conn_map_ was indexed with.
        //
        // The erase races with user-thread shared-lock readers in the
        // DoRequest fast-path; take the unique lock around the modification
        // so the readers never observe a half-rehashed bucket.
        {
            std::unique_lock<std::shared_mutex> wlock(conn_map_mu_);
            for (auto it = conn_map_.begin(); it != conn_map_.end(); ++it) {
                if (it->second && it->second->GetQuicConnection().get() == conn.get()) {
                    conn_map_.erase(it);
                    break;
                }
            }
        }



        // If we are in graceful-shutdown, accelerate Destroy() as soon as all
        // observed connections have closed — avoids the 1s tax per Close().
        // We only decrement while is_closing_ so that stray close events from
        // failed or idle-timeout connections during normal operation don't
        // underflow the counter or trigger a premature Destroy().
        //
        // NOTE: Destroy() is scheduled on the event loop (0ms timer) rather than
        // invoked synchronously here. We are inside the QUIC connection's close
        // path (BaseConnection::OnStateToClosing/Draining -> ... -> this callback),
        // and synchronously tearing down the owning quic client from that frame
        // would invalidate the event_loop/connection_closer that the caller still
        // uses after we return (timer registration, log writes, etc.).
        if (is_closing_ && pending_close_count_ > 0) {
            --pending_close_count_;
            if (pending_close_count_ == 0 && !destroy_scheduled_) {
                destroy_scheduled_ = true;
                LOG_DEBUG("Client: all connections closed, destroying quic client immediately");
                quic_->AddTimer(0, [this]() { quic_->Destroy(); });
            }
        }

        // Clear waiting requests for this address (connection failed)
        auto wait_it = wait_request_map_.find(addr_key);
        if (wait_it != wait_request_map_.end()) {
            // Notify all waiting requests about the connection failure
            uint32_t error_code = error != 0 ? error : Http3ErrorCode::kInternalError;
            while (!wait_it->second.empty()) {
                auto& context = wait_it->second.front();
                
                // Call the user's response callback with error
                if (context.IsAsync()) {
                    auto handler = context.GetAsyncHandler();
                    if (handler) {
                        handler->OnError(error_code);
                    }
                } else {
                    auto handler = context.GetCompleteHandler();
                    if (handler) {
                        handler(nullptr, error_code);
                    }
                }
                
                // Also call error handler if available
                if (error_handler_) {
                    error_handler_(context.host, error_code);
                }
                wait_it->second.pop();
            }
            wait_request_map_.erase(wait_it);
        }
        return;
    }

    // Connection failed to create
    if (error != 0) {
        LOG_ERROR("connection creation failed. error: %d, reason: %s", error, reason.c_str());
        // Handle connection failure - notify all waiting requests
        auto wait_it = wait_request_map_.find(addr_key);
        if (wait_it != wait_request_map_.end()) {
            while (!wait_it->second.empty()) {
                auto& context = wait_it->second.front();
                
                // Call the user's response callback with error
                if (context.IsAsync()) {
                    auto handler = context.GetAsyncHandler();
                    if (handler) {
                        handler->OnError(error);
                    }
                } else {
                    auto handler = context.GetCompleteHandler();
                    if (handler) {
                        handler(nullptr, error);
                    }
                }
                
                // Also call error handler if available
                if (error_handler_) {
                    error_handler_(context.host, error);
                }
                wait_it->second.pop();
            }
            wait_request_map_.erase(wait_it);
        }
        return;
    }

    // Connection established successfully
    auto wait_it = wait_request_map_.find(addr_key);
    if (wait_it == wait_request_map_.end() || wait_it->second.empty()) {
        LOG_ERROR("no wait request context found for connection. addr: %s", addr_key.c_str());
        return;
    }

    // Get the first context to determine the host name for connection mapping
    auto first_context = wait_it->second.front();
    std::string host_name = first_context.host;

    // Create client connection
    auto client_conn = std::make_shared<ClientConnection>(host_name, settings_, conn,
        std::bind(&Client::HandleError, this, std::placeholders::_1, std::placeholders::_2),
        std::bind(&Client::HandlePushPromise, this, std::placeholders::_1),
        std::bind(&Client::HandlePush, this, std::placeholders::_1, std::placeholders::_2),
        config_.max_concurrent_streams_, config_.enable_push_);

    // Initialize connection (starts timers)
    client_conn->Init();

    // Store connection in map (held under unique lock so user-thread
    // fast-path readers never see a half-rehashed bucket).
    {
        std::unique_lock<std::shared_mutex> wlock(conn_map_mu_);
        conn_map_[host_name] = client_conn;
    }

    // Process all waiting requests in the queue
    while (!wait_it->second.empty()) {
        auto& context = wait_it->second.front();

        // Send request with the appropriate handler type
        if (context.IsAsync()) {
            client_conn->DoRequest(context.request, context.GetAsyncHandler());
        } else {
            client_conn->DoRequest(context.request, context.GetCompleteHandler());
        }

        wait_it->second.pop();
    }

    // Remove the empty queue from wait_request_map_
    wait_request_map_.erase(wait_it);
}

void Client::HandleError(const std::string& unique_id, uint32_t error_code) {
    // H3_NO_ERROR (0x100 = 256) indicates graceful closure, not a real error
    if (error_code == static_cast<uint32_t>(Http3ErrorCode::kNoError)) {
        LOG_DEBUG("handle graceful close. unique_id: %s", unique_id.c_str());
    } else {
        LOG_ERROR("handle error. unique_id: %s, error_code: %d", unique_id.c_str(), error_code);
    }
    {
        std::unique_lock<std::shared_mutex> wlock(conn_map_mu_);
        conn_map_.erase(unique_id);
    }
    if (error_handler_) {
        error_handler_(unique_id, error_code);
    }
}

bool Client::HandlePushPromise(std::unordered_map<std::string, std::string>& headers) {
    if (!push_promise_handler_) {
        return false;
    }
    return push_promise_handler_(headers);
}

void Client::HandlePush(std::shared_ptr<IResponse> response, uint32_t error) {
    if (!push_handler_) {
        return;
    }
    push_handler_(response, error);
}

void Client::SetPushPromiseHandler(const http_push_promise_handler& push_promise_handler) {
    push_promise_handler_ = push_promise_handler;
}

void Client::SetPushHandler(const http_response_handler& push_handler) {
    push_handler_ = push_handler;
}

void Client::SetErrorHandler(const error_handler& error_handler) {
    error_handler_ = error_handler;
}

void Client::Close() {
    // Close() must complete synchronously: ~Client() invokes Close() then
    // quic_->Join(), and callers (e.g. test fixture TearDown) expect the
    // shutdown work to be observable before client_.reset() returns. Hopping
    // the body onto the QUIC loop via AddTimer(0, ...) made Close() return
    // immediately while the lambda was still queued, so the captured `this`
    // could outlive the Client object — leading to use-after-free during
    // teardown. We instead run inline and rely on conn_map_mu_ + snapshot
    // copy to avoid racing OnConnection / HandleError.

    // Guard against double-close: if Close() was already invoked the
    // Destroy() timer is already scheduled; registering a second one
    // would lead to double-free corruption.
    if (is_closing_) {
        return;
    }
    is_closing_ = true;

    // Snapshot every active ClientConnection under the conn_map_ lock
    // *before* invoking Close() on any of them. ClientConnection::Close
    // may synchronously trigger BaseConnection's close path, which
    // re-enters Client::OnConnection(kConnectionClose) and tries to
    // erase the matching entry from conn_map_ — invalidating the
    // iterator we would otherwise be holding. By copying the
    // shared_ptrs out first, we both:
    //   1. release conn_map_mu_ before we call any user code (avoids
    //      lock inversion / re-entrant lock-on-the-same-thread bugs),
    //   2. keep each ClientConnection alive via the local vector even
    //      if OnConnection erases it from conn_map_ mid-loop.
    std::vector<std::shared_ptr<ClientConnection>> snapshot;
    {
        std::shared_lock<std::shared_mutex> rlock(conn_map_mu_);
        snapshot.reserve(conn_map_.size());
        for (auto& pair : conn_map_) {
            if (pair.second) {
                snapshot.push_back(pair.second);
            }
        }
    }

    LOG_INFO("Client::Close() - gracefully closing all connections (%zu active)", snapshot.size());

    // Count how many quic connections we are waiting to observe close
    // for. We use this to short-circuit the fallback destroy timer when
    // all peers have acknowledged the CONNECTION_CLOSE (or the idle path
    // has torn them down), so that short-lived clients (e.g. one
    // request + Close()) do not pay the full
    // kConnectionCloseDestroyTimeoutMs (1s) tax on destruction.
    pending_close_count_ = static_cast<uint32_t>(snapshot.size());

    // Close all active HTTP/3 connections from the snapshot. Even if a
    // synchronous CONNECTION_CLOSE callback erases the matching entry
    // from conn_map_ during the call, the snapshot keeps the
    // ClientConnection alive for the duration of this loop.
    for (auto& conn : snapshot) {
        conn->Close(0);  // error_code=0 means normal close
    }
    snapshot.clear();

    if (pending_close_count_ == 0) {
        // No live connections at all (e.g. Close() before any
        // DoRequest). Destroy immediately — nothing to drain.
        if (!destroy_scheduled_) {
            destroy_scheduled_ = true;
            quic_->Destroy();
        }
        return;
    }

    // Safety-net fallback: if CONNECTION_CLOSE cannot be flushed within
    // kConnectionCloseDestroyTimeoutMs (peer unresponsive, socket
    // blocked, etc.) we still force-destroy. OnConnection() below will
    // short-circuit this when all connections have genuinely closed
    // first.
    quic_->AddTimer(kConnectionCloseDestroyTimeoutMs, [this]() {
        if (!destroy_scheduled_) {
            destroy_scheduled_ = true;
            quic_->Destroy();
        }
    });
}

bool Client::InitiateMigration() {
    bool result = false;

    // Snapshot the connection set under the shared lock so we never race
    // OnConnection's insert/erase. Callouts to ClientConnection happen with
    // the lock released to avoid holding it across user code.
    std::vector<std::pair<std::string, std::shared_ptr<ClientConnection>>> snapshot;
    {
        std::shared_lock<std::shared_mutex> rlock(conn_map_mu_);
        snapshot.reserve(conn_map_.size());
        for (auto& pair : conn_map_) {
            if (pair.second) snapshot.emplace_back(pair.first, pair.second);
        }
    }

    LOG_INFO("Client::InitiateMigration() - initiating migration on %zu connections", snapshot.size());

    for (auto& pair : snapshot) {
        LOG_DEBUG("Initiating migration on connection to %s", pair.first.c_str());
        if (pair.second->InitiateMigration()) {
            result = true;
            LOG_INFO("Migration initiated on connection to %s", pair.first.c_str());
        } else {
            LOG_WARN("Migration failed on connection to %s", pair.first.c_str());
        }
    }

    return result;
}

MigrationResult Client::InitiateMigrationTo(const std::string& local_ip, uint16_t local_port) {
    std::vector<std::pair<std::string, std::shared_ptr<ClientConnection>>> snapshot;
    {
        std::shared_lock<std::shared_mutex> rlock(conn_map_mu_);
        snapshot.reserve(conn_map_.size());
        for (auto& pair : conn_map_) {
            if (pair.second) snapshot.emplace_back(pair.first, pair.second);
        }
    }

    LOG_INFO("Client::InitiateMigrationTo() - initiating migration to %s:%d on %zu connections",
        local_ip.c_str(), local_port, snapshot.size());

    if (snapshot.empty()) {
        LOG_WARN("Client::InitiateMigrationTo: no active connections");
        return MigrationResult::kFailedInvalidState;
    }

    // Initiate migration on the first (or all) active HTTP/3 connection(s)
    // For simplicity, we initiate on the first connection. In production,
    // you might want to migrate all connections or a specific one.
    for (auto& pair : snapshot) {
        LOG_DEBUG(
            "Initiating migration to %s:%d on connection to %s", local_ip.c_str(), local_port, pair.first.c_str());
        auto result = pair.second->InitiateMigrationTo(local_ip, local_port);
        if (result == MigrationResult::kSuccess) {
            LOG_INFO("Migration initiated on connection to %s", pair.first.c_str());
            return result;
        } else {
            LOG_WARN(
                "Migration failed on connection to %s with result %d", pair.first.c_str(), static_cast<int>(result));
        }
    }

    return MigrationResult::kFailedInvalidState;
}

void Client::SetMigrationCallback(migration_callback cb) {
    migration_cb_ = cb;

    // Snapshot, then forward to all existing connections without holding the
    // lock across user code.
    std::vector<std::shared_ptr<ClientConnection>> snapshot;
    {
        std::shared_lock<std::shared_mutex> rlock(conn_map_mu_);
        snapshot.reserve(conn_map_.size());
        for (auto& pair : conn_map_) {
            if (pair.second) snapshot.push_back(pair.second);
        }
    }
    for (auto& conn : snapshot) {
        conn->SetMigrationCallback(cb);
    }
}

}  // namespace http3
}  // namespace quicx
