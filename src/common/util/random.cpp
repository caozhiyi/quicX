#include <random>
#include "random.h"

namespace quicx {
namespace common {

std::random_device RangeRandom::_random;
std::mt19937 RangeRandom::_engine(_random());

RangeRandom::RangeRandom(int32_t min, int32_t max):
    _uniform(min, max) {

}

RangeRandom::~RangeRandom() {

}

int32_t RangeRandom::Random() {
    return _uniform(_engine);
}

}
}