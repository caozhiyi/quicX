#include <chrono>
#include <memory>

#include "common/log/log.h"
#include "common/network/if_event_driver.h"
#include "common/network/io_handle.h"

#include "upgrade/handlers/smart_handler_factory.h"
#include "upgrade/server/connection_handler.h"
#include "upgrade/server/upgrade_server.h"

namespace quicx {
namespace upgrade {

namespace {
// How long a caller waits for the loop thread to execute posted work.
// Generous on purpose: these are startup/teardown paths, and the alternative
// to a timeout is hanging forever if the loop thread ever dies.
constexpr std::chrono::seconds kWorkTimeout{5};
constexpr std::chrono::seconds kTeardownTimeout{2};
}  // namespace

}  // namespace quicx

// Factory for the public quicx::IUpgrade interface. Must be defined in a
// namespace enclosing quicx::IUpgrade (i.e. quicx itself), not in quicx::upgrade.
std::unique_ptr<IUpgrade> IUpgrade::MakeUpgrade() {
    return std::make_unique<upgrade::UpgradeServer>();
}

namespace upgrade {

UpgradeServer::UpgradeServer() = default;

UpgradeServer::~UpgradeServer() {
    Stop();
}

bool UpgradeServer::AddListener(UpgradeSettings& settings) {
    // The caller blocks on a future while the work runs on the loop thread,
    // so capturing `settings` by reference is safe.
    return RunOnLoopThread([this, &settings]() { return AddListenerOnLoopThread(settings); });
}

void UpgradeServer::Stop() {
    std::thread thread_to_join;
    std::shared_ptr<common::IEventLoop> loop;
    {
        std::lock_guard<std::mutex> lk(mu_);
        if (!loop_thread_.joinable()) {
            // Never started (or already stopped): nothing to tear down.
            stopped_ = true;
            return;
        }
        if (event_loop_ && event_loop_->IsInLoopThread()) {
            // Called from inside a loop-thread callback: we would post the
            // teardown to ourselves and then join ourselves. Leave the work
            // to the destructor running on the owner thread instead.
            LOG_WARN("upgrade: Stop() from the loop thread ignored; call it from the owner thread");
            return;
        }
        stopped_ = true;
        thread_to_join = std::move(loop_thread_);
        loop = event_loop_;
    }

    // Unregister + close on the loop thread: RemoveFd() calls
    // AssertInLoopThread() and aborts the process if invoked from anywhere
    // else. Doing it here (rather than in the destructor, which runs on the
    // owner's thread) is what keeps shutdown from racing the loop's own
    // teardown and dispatching to an already-destroyed ConnectionHandler.
    bool torn_down = false;
    if (loop) {
        std::promise<void> done;
        auto fut = done.get_future();
        loop->PostTask([this, &done]() {
            CloseAllOnLoopThread();
            done.set_value();
        });
        torn_down = (fut.wait_for(kTeardownTimeout) == std::future_status::ready);
        if (!torn_down) {
            LOG_WARN("upgrade: loop thread did not run teardown in time; closing sockets directly");
        }
    }

    // Stop the pump only after the teardown task has run: Wait() drains the
    // task queue at the end of each iteration, so posting first and then
    // clearing the flag guarantees the ordering.
    running_.store(false, std::memory_order_release);
    if (loop) {
        loop->Wakeup();
    }
    thread_to_join.join();

    if (!torn_down) {
        CloseListenerFds();
    }

    // Safe now: the loop thread is joined, so nothing can touch it.
    std::lock_guard<std::mutex> lk(mu_);
    event_loop_.reset();
}

bool UpgradeServer::RunOnLoopThread(const std::function<bool()>& work) {
    std::lock_guard<std::mutex> lk(mu_);
    if (stopped_) {
        LOG_ERROR("upgrade server has been stopped; refusing new listener");
        return false;
    }
    if (!StartLoopThreadLocked()) {
        return false;
    }

    std::promise<bool> done;
    auto fut = done.get_future();
    event_loop_->PostTask([&work, &done]() { done.set_value(work()); });
    if (fut.wait_for(kWorkTimeout) != std::future_status::ready) {
        LOG_ERROR("upgrade: event loop did not execute the request in time");
        return false;
    }
    return fut.get();
}

bool UpgradeServer::StartLoopThreadLocked() {
    if (loop_thread_.joinable()) {
        return true;
    }
    if (start_failed_) {
        return false;
    }

    event_loop_ = common::MakeEventLoop();
    if (!event_loop_) {
        LOG_ERROR("upgrade: failed to create event loop");
        start_failed_ = true;
        return false;
    }

    // EventLoop::Init() records the calling thread's id and every later
    // RegisterFd/AddTimer asserts against it, so Init() MUST run on the
    // thread that will drive Wait().
    std::promise<bool> ready;
    auto ready_fut = ready.get_future();
    running_.store(true, std::memory_order_release);
    loop_thread_ = std::thread([this, p = std::move(ready)]() mutable { LoopThreadMain(std::move(p)); });

    const bool ok = ready_fut.get();
    if (!ok) {
        LOG_ERROR("upgrade: failed to init event loop");
        running_.store(false, std::memory_order_release);
        loop_thread_.join();
        loop_thread_ = std::thread();
        start_failed_ = true;
        return false;
    }
    return true;
}

void UpgradeServer::LoopThreadMain(std::promise<bool> ready) {
    const bool ok = event_loop_ && event_loop_->Init();
    ready.set_value(ok);
    if (!ok) {
        return;
    }
    while (running_.load(std::memory_order_acquire)) {
        event_loop_->Wait();
    }
}

bool UpgradeServer::AddListenerOnLoopThread(UpgradeSettings& settings) {
    auto loop = event_loop_;
    if (!loop) return false;

    // The upgrade module is meant to live next to a real H3/QUIC server and
    // advertise it via Alt-Svc on TCP. To do that we need BOTH a plaintext
    // socket (so a browser's first http://host:port/ request gets a 200 +
    // Alt-Svc) and a TLS socket (so https://host:port/ also works and can
    // negotiate h2/http1.1 via ALPN). The previous implementation picked
    // ONE port based on whether credentials were configured, which meant
    // that as soon as you handed the server a cert+key, port 8080 was
    // silently never bound -- chrome/curl on http:// got "connection
    // refused" and h3 was undiscoverable.
    //
    // New behaviour: bind whichever of {http_port_, https_port_} the caller
    // actually populated, with the appropriate handler for each. Each
    // listener owns its own ConnectionHandler / ISmartHandler pair so the
    // plaintext path can never accidentally feed bytes into an SSL state
    // machine and vice versa.
    const bool has_file_pair = !settings.cert_file_.empty() && !settings.key_file_.empty();
    const bool has_pem_pair = (settings.cert_pem_ != nullptr) && (settings.key_pem_ != nullptr);
    const bool https_enabled = has_file_pair || has_pem_pair;

    bool started_any = false;

    auto bind_one = [&](uint16_t port, SmartHandlerFactory::HandlerKind kind, const char* label) -> bool {
        if (port == 0) {
            return true;  // not requested, skip silently
        }
        // No upper-bound check: port is uint16_t, so 65535 is already its maximum.
        auto handler = SmartHandlerFactory::CreateHandler(settings, loop, kind);
        if (!handler) {
            LOG_ERROR("Failed to create %s handler", label);
            return false;
        }
        auto connection_handler = std::make_shared<ConnectionHandler>(loop, handler);

        int listen_fd = CreateListenSocket(settings.listen_addr_, port);
        if (listen_fd < 0) {
            LOG_ERROR("Failed to create %s listening socket on %s:%d", label, settings.listen_addr_.c_str(), port);
            return false;
        }

        if (loop->RegisterFd(listen_fd, common::EventType::ET_READ, connection_handler)) {
            LOG_INFO("%s listener added on %s:%d", handler->GetType().c_str(), settings.listen_addr_.c_str(), port);
        } else {
            LOG_ERROR(
                "Failed to add %s listener on %s:%d", handler->GetType().c_str(), settings.listen_addr_.c_str(), port);
            common::Close(listen_fd);
            return false;
        }
        // CRITICAL: keep a strong reference to the ConnectionHandler that
        // services this fd. EventLoop::fd_to_handler_ stores it as a
        // std::weak_ptr, so without this push_back the shared_ptr falls
        // off the end of bind_one() and the very next epoll/kqueue wakeup
        // for `listen_fd` produces "No handler found for fd N" and the
        // accept() loop never runs. The smart handler that connection_handler
        // points at is transitively kept alive through ConnectionHandler::
        // handler_ — that path is also what keeps client_fd's registration
        // valid (client fds register `handler_` directly on the loop).
        listeners_.push_back(ListenEntry{static_cast<uint32_t>(listen_fd), connection_handler});
        started_any = true;
        return true;
    };

    // Plaintext H1/H2 listener (used to serve Alt-Svc to browsers that hit
    // http:// first).
    if (!bind_one(settings.http_port_, SmartHandlerFactory::HandlerKind::kHttp, "HTTP")) {
        return false;
    }

    // TLS listener (only meaningful if a cert/key is configured).
    if (https_enabled) {
        if (!bind_one(settings.https_port_, SmartHandlerFactory::HandlerKind::kHttps, "HTTPS")) {
            return false;
        }
    } else if (settings.https_port_ != 0) {
        LOG_WARN("https_port=%u was set but no cert/key configured; skipping TLS listener", settings.https_port_);
    }

    if (!started_any) {
        LOG_ERROR("AddListener: neither http_port nor https_port were usable");
        return false;
    }

    LOG_INFO("Listener added successfully");
    return true;
}

void UpgradeServer::CloseAllOnLoopThread() {
    auto loop = event_loop_;
    for (auto& entry : listeners_) {
        if (loop) {
            loop->RemoveFd(entry.fd);
        }
        // Drop every client connection first: their fds are registered on
        // the same loop and would otherwise outlive the listener.
        if (entry.handler) {
            entry.handler->CloseAllConnections();
        }
        common::Close(entry.fd);
    }
    listeners_.clear();
}

void UpgradeServer::CloseListenerFds() {
    for (auto& entry : listeners_) {
        common::Close(entry.fd);
    }
    listeners_.clear();
}

int UpgradeServer::CreateListenSocket(const std::string& addr, uint16_t port) {
    // Create socket
    auto result = common::TcpSocket();
    if (result.error_code_ != 0) {
        LOG_ERROR("Failed to create socket. errno: %d", result.error_code_);
        return -1;
    }
    uint64_t listen_fd = result.return_value_;

    // Set non-blocking
    auto ret = common::SocketNoblocking(listen_fd);
    if (ret.error_code_ != 0) {
        LOG_ERROR("Failed to set socket flags. errno: %d", ret.error_code_);
        common::Close(listen_fd);
        return -1;
    }

    // Bind socket
    common::Address address(addr, port);
    ret = common::Bind(listen_fd, address);
    if (ret.error_code_ != 0) {
        LOG_ERROR("Failed to bind socket. errno: %d", ret.error_code_);
        common::Close(listen_fd);
        return -1;
    }

    ret = common::Listen(listen_fd, 1024);
    if (ret.error_code_ != 0) {
        LOG_ERROR("Failed to listen on socket. errno: %d", ret.error_code_);
        common::Close(listen_fd);
        return -1;
    }

    LOG_INFO("Listening socket created on %s:%d", addr.c_str(), port);
    return listen_fd;
}

}  // namespace upgrade
}  // namespace quicx
