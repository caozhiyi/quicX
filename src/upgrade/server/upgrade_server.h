#ifndef UPGRADE_SERVER_UPGRADE_SERVER_H
#define UPGRADE_SERVER_UPGRADE_SERVER_H

#include <atomic>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <quicx/upgrade/if_upgrade.h>
#include <thread>
#include <utility>
#include <vector>

#include "common/network/if_event_loop.h"

#include "upgrade/server/connection_handler.h"

namespace quicx {
namespace upgrade {

// Main upgrade server implementation
//
// Threading model: one private EventLoop plus one thread that runs it. Every
// fd operation (RegisterFd / ModifyFd / RemoveFd / AddTimer) MUST happen on
// that thread -- EventLoop::AssertInLoopThread() aborts otherwise -- so all
// mutating entry points funnel through RunOnLoopThread().
//
// `listeners_` and the handlers it owns are only ever touched on the loop
// thread, which is what makes teardown deterministic.
class UpgradeServer: public IUpgrade {
public:
    UpgradeServer();
    virtual ~UpgradeServer();

    // Add listener with specified settings
    virtual bool AddListener(UpgradeSettings& settings) override;

    // Stop the loop thread and release every socket
    virtual void Stop() override;

private:
    // Create listening socket
    int CreateListenSocket(const std::string& addr, uint16_t port);

    // Runs `work` on the loop thread and blocks until it completes. Starts
    // the loop thread on first use.
    bool RunOnLoopThread(const std::function<bool()>& work);

    // Caller must hold mu_. Creates the loop + thread and waits for Init().
    bool StartLoopThreadLocked();

    void LoopThreadMain(std::promise<bool> ready);

    // Both run on the loop thread only.
    bool AddListenerOnLoopThread(UpgradeSettings& settings);
    void CloseAllOnLoopThread();

    // Last-resort cleanup for the case where the loop thread never got to
    // run its teardown task (e.g. it died during Init()).
    void CloseListenerFds();

    // EventLoop::fd_to_handler_ stores handlers as std::weak_ptr, so the
    // upgrade server itself MUST own a strong reference to every
    // ConnectionHandler we register, otherwise the shared_ptr created
    // inside AddListener() dies as soon as the local variable goes out of
    // scope and the next epoll/kqueue wakeup logs
    //   "No handler found for fd N"   (or "Handler expired for fd N")
    // and silently drops the accept(). Each entry pairs the listening fd
    // with the ConnectionHandler that should service it; both are released
    // together in Stop().
    struct ListenEntry {
        uint32_t fd = 0;
        std::shared_ptr<ConnectionHandler> handler;
    };

    std::mutex mu_;
    std::shared_ptr<common::IEventLoop> event_loop_;
    std::thread loop_thread_;
    std::atomic<bool> running_{false};
    bool start_failed_ = false;  // loop could not be created / initialized
    bool stopped_ = false;

    // Guarded by the loop thread: only AddListenerOnLoopThread() /
    // CloseAllOnLoopThread() / CloseListenerFds() touch it.
    std::vector<ListenEntry> listeners_;
};

}  // namespace upgrade
}  // namespace quicx

#endif  // UPGRADE_SERVER_UPGRADE_SERVER_H
