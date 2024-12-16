#include <random>
#include "common/util/random.h"

namespace quicx {
namespace common {

std::random_device RangeRandom::random_;
std::mt19937 RangeRandom::engine_(random_());

RangeRandom::RangeRandom(int32_t min, int32_t max):
    uniform_(min, max) {

}

RangeRandom::~RangeRandom() {

}

int32_t RangeRandom::Random() {
    return uniform_(engine_);
}

}
}