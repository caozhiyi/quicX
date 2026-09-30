#ifndef TOOL_QC_FEATURES_MIGRATION_CTL_H
#define TOOL_QC_FEATURES_MIGRATION_CTL_H

#include "cli/options.h"
#include "obs/tracer.h"

namespace quicx {
class IClient;
}

// Connection-migration demo controller (--migrate family).
//   --migrate                  : migrate to a new local port after transfer
//   --migrate-delay <ms>       : instead trigger migration mid-transfer
//   --migrate-to <ip[:port]>   : explicit new local address
// The migration callback prints old/new quadruples + path validation result
// so the demo output explains itself.
class MigrationCtl {
public:
    MigrationCtl(const TransportOptions& tp, const Tracer& tracer);

    // Prints a warning when the configured demo cannot run.
    void CheckPreconditions() const;

    // Blocks ~delay_ms then triggers migration (call from a helper thread).
    void TriggerDelayed(quicx::IClient& client) const;

    // Immediate migration (default post-transfer demo).
    void TriggerNow(quicx::IClient& client) const;

    static void InstallCallback(quicx::IClient& client, const Tracer& tracer);

private:
    TransportOptions tp_;
    Tracer tracer_;
};

#endif  // TOOL_QC_FEATURES_MIGRATION_CTL_H
