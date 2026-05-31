#include "common/network/socket_family_cache.h"

#include <mutex>
#include <unordered_map>

namespace quicx {
namespace common {

namespace {

// Hot-path-friendly: a single mutex + open-addressed hashmap is plenty for
// the modest number of UDP sockets a single QUIC endpoint manages
// (typically: one per worker + one per in-flight migration). The map is
// only touched from Bind(), SendTo() and the create/close pair, never per
// packet on the steady-state datapath because SendTo() short-circuits via
// the connected-socket family that's been written here once.
std::mutex& Mutex() {
    static std::mutex m;
    return m;
}

std::unordered_map<int32_t, int32_t>& Map() {
    static std::unordered_map<int32_t, int32_t> m;
    return m;
}

}  // namespace

void RememberSocketFamily(int32_t fd, int32_t family) {
    if (fd < 0) return;
    std::lock_guard<std::mutex> lk(Mutex());
    Map()[fd] = family;
}

void ForgetSocketFamily(int32_t fd) {
    if (fd < 0) return;
    std::lock_guard<std::mutex> lk(Mutex());
    Map().erase(fd);
}

int32_t GetSocketFamily(int32_t fd) {
    if (fd < 0) return 0;
    std::lock_guard<std::mutex> lk(Mutex());
    auto it = Map().find(fd);
    if (it == Map().end()) return 0;
    return it->second;
}

}  // namespace common
}  // namespace quicx
