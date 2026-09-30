#include "features/migration_ctl.h"

#include <chrono>
#include <thread>

#include <quicx/http3/if_client.h>
#include <quicx/quic/type.h>

namespace {

// "127.0.0.1", "127.0.0.1:0", "" -> {ip, port}
std::pair<std::string, uint16_t> ParseMigrateTarget(const std::string& spec) {
    if (spec.empty()) return {"", 0};
    size_t colon = spec.rfind(':');
    if (colon == std::string::npos) return {spec, 0};
    return {spec.substr(0, colon), static_cast<uint16_t>(std::stoi(spec.substr(colon + 1)))};
}

}  // namespace

MigrationCtl::MigrationCtl(const TransportOptions& tp, const Tracer& tracer)
    : tp_(tp), tracer_(tracer) {}

void MigrationCtl::CheckPreconditions() const {
    if (tp_.migrate && tp_.migrate_delay_ms > 0)
        tracer_.Info("migration demo: will trigger %u ms into the transfer", tp_.migrate_delay_ms);
}

void MigrationCtl::TriggerDelayed(quicx::IClient& client) const {
    std::this_thread::sleep_for(std::chrono::milliseconds(tp_.migrate_delay_ms));
    auto [ip, port] = ParseMigrateTarget(tp_.migrate_to);
    tracer_.Info("initiating mid-transfer migration (delay elapsed)");
    auto result = client.InitiateMigrationTo(ip, port);
    tracer_.Info("migration initiate result: %s",
        result == quicx::MigrationResult::kSuccess ? "accepted" : "rejected");
}

void MigrationCtl::TriggerNow(quicx::IClient& client) const {
    auto [ip, port] = ParseMigrateTarget(tp_.migrate_to);
    tracer_.Info("initiating migration to %s:%u (empty ip = same host, new port)", ip.c_str(), port);
    auto result = client.InitiateMigrationTo(ip, port);
    tracer_.Info("migration initiate result: %s",
        result == quicx::MigrationResult::kSuccess ? "accepted" : "rejected");
}

void MigrationCtl::InstallCallback(quicx::IClient& client, const Tracer& tracer) {
    client.SetMigrationCallback([&tracer](std::shared_ptr<quicx::IQuicConnection>, const quicx::MigrationInfo& info) {
        const char* verdict = info.result_ == quicx::MigrationResult::kSuccess ? "SUCCESS" : "FAILED";
        std::fprintf(stderr, "* migration %s: %s:%u -> %s:%u (peer %s:%u, path validated, %.1f ms)\n",
            verdict, info.old_local_ip_.c_str(), info.old_local_port_, info.new_local_ip_.c_str(),
            info.new_local_port_, info.new_peer_ip_.c_str(), info.new_peer_port_,
            static_cast<double>(info.migration_end_time_ - info.migration_start_time_) / 1000.0);
    });
    (void)tracer;
}
