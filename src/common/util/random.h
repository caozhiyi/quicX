#ifndef COMMON_UTIL_RANDOM
#define COMMON_UTIL_RANDOM

#include <random>
#include <cstdint>

namespace quicx {
namespace common {

class RangeRandom {
public:
    RangeRandom(int32_t min, int32_t max);
    ~RangeRandom();

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