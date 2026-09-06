#ifndef UPGRADE_INCLUDE_IF_UPGRADE
#define UPGRADE_INCLUDE_IF_UPGRADE

#include <memory>
#include <quicx/upgrade/type.h>

namespace quicx {

// HTTP upgrade server interface
//
// The server owns a private event loop and the single thread that drives it.
// Callers never see an event loop and therefore never have to satisfy
// EventLoop's thread-affinity contract (Init / RegisterFd / Wait must all
// happen on one and the same thread): AddListener() hands the bind work to
// that thread and blocks until it is done, and Stop() tears the sockets down
// on the same thread before joining it.
//
// Nothing is shared with the QUIC/H3 server: clients discover h3 through
// Alt-Svc on TCP and then open a brand new UDP connection, so there is no
// benefit (and real cost in failure isolation) in co-tenanting one loop.
class IUpgrade {
public:
    IUpgrade() = default;
    virtual ~IUpgrade() = default;

    // Add a listener with specified settings.
    //
    // Callable from any thread. Returns false when the loop cannot be
    // started, the ports cannot be bound, or the server was already stopped.
    virtual bool AddListener(UpgradeSettings& settings) = 0;

    // Stop the internal loop thread, unregister and close every listener and
    // client socket. Idempotent; also invoked by the destructor.
    virtual void Stop() = 0;

    // Create a server instance. No thread is created until AddListener().
    static std::unique_ptr<IUpgrade> MakeUpgrade();
};

}  // namespace quicx

#endif  // UPGRADE_INCLUDE_IF_UPGRADE
