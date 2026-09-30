#ifndef TOOL_QC_SESSION_SESSION_H
#define TOOL_QC_SESSION_SESSION_H

#include <memory>

#include "cli/options.h"

namespace quicx {
class IClient;
}

// Owns the library client lifecycle. The ONLY place that talks to IClient
// construction/Init/Close. Higher layers get a raw pointer via Get().
class ClientSession {
public:
    ClientSession() = default;
    ~ClientSession();

    ClientSession(const ClientSession&) = delete;
    ClientSession& operator=(const ClientSession&) = delete;

    bool Init(const TransportOptions& tp, double connect_timeout_s, bool verbose);

    // May be null if Init failed.
    quicx::IClient* Get() const { return client_.get(); }

    // Graceful close (sends CONNECTION_CLOSE, flushes qlog/keylog/session cache).
    void Close();

private:
    std::unique_ptr<quicx::IClient> client_;
    bool inited_ = false;
};

#endif  // TOOL_QC_SESSION_SESSION_H
