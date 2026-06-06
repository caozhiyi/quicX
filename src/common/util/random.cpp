#include <random>
#include "common/util/random.h"

namespace quicx {
namespace common {

// NON-CRYPTOGRAPHIC. See class-level comment in random.h: this engine must
// not be used for any security-sensitive draws (CIDs, tokens, keys, etc.).
//
// Per-thread Mersenne-Twister engine. Each thread gets its own state seeded
// once from std::random_device on first call, eliminating the cross-thread
// data race TSan reported on the previous shared static engine.
std::mt19937& RangeRandom::Engine() {
    thread_local std::mt19937 engine{std::random_device{}()};
    return engine;
}

RangeRandom::RangeRandom(int32_t min, int32_t max):
    uniform_(min, max) {

}

RangeRandom::~RangeRandom() {

}

int32_t RangeRandom::Random() {
    return uniform_(Engine());
}

}
}