#ifndef COMMON_UTIL_RANDOM
#define COMMON_UTIL_RANDOM

#include <random>
#include <cstdint>

namespace quicx {
namespace common {

// RangeRandom: NON-CRYPTOGRAPHIC pseudo-random integer generator.
//
// Backed by std::mt19937 (Mersenne Twister), which is deterministic given its
// seed and is NOT suitable for any security-sensitive purpose. Do NOT use this
// class for:
//   * connection IDs
//   * stateless reset tokens
//   * Retry tokens / packet number encryption nonces
//   * TLS keys, IVs, or any cryptographic material
//   * authentication tokens, session secrets, password salts
//
// For those, use a CSPRNG such as BoringSSL/OpenSSL `RAND_bytes` (see how
// `ConnectionIDGenerator` and `RetryTokenManager` source their entropy).
//
// Intended uses are statistical / non-adversarial only, e.g. timer jitter,
// load-balancing tie-breakers, fuzz / simulation inputs, exponential backoff
// randomization.
class RangeRandom {
public:
    RangeRandom(int32_t min, int32_t max);
    ~RangeRandom();

    // Returns a non-cryptographic pseudo-random integer in [min, max].
    int32_t Random();

private:
    // mt19937 is NOT thread-safe (TSan flags concurrent use of _M_gen_rand
    // when several worker threads call Random() in parallel — observed on
    // the timing-wheel hot path). We therefore keep one engine per thread
    // instead of guarding a single shared engine with a mutex (faster and
    // avoids cross-thread contention on every random draw). Each engine is
    // independently seeded from std::random_device on first use.
    static std::mt19937& Engine();

    std::uniform_int_distribution<int32_t> uniform_;
};

}
}

#endif