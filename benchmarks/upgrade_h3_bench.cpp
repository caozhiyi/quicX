#if defined(QUICX_ENABLE_BENCHMARKS)
#include <benchmark/benchmark.h>
#include <thread>
#include <memory>

#include "upgrade/include/if_upgrade.h"

namespace quicx {
namespace upgrade {

static void BM_UpgradeH3_Server_Run(benchmark::State& state) {
    // Minimal server run-stop cycle to measure overhead
    for (auto _ : state) {
        auto srv = IUpgrade::MakeUpgrade();
        srv->Init(LogLevel::kError);
        UpgradeSettings set; set.listen_addr = "127.0.0.1"; set.h3_port = 8891; set.enable_http3 = true; set.enable_http1 = false; set.enable_http2 = false;
        srv->AddListener(set);
        std::thread th([&](){ srv->Join(); });
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        srv->Stop();
        th.join();
    }
}

} // namespace upgrade
} // namespace quicx

BENCHMARK(quicx::upgrade::BM_UpgradeH3_Server_Run);
BENCHMARK_MAIN();
#else
int main() { return 0; }
#endif


