#include <iostream>
#include "time_consuming.h"

namespace quicx {

TimeConsuming::TimeConsuming() {
    _clock = std::chrono::time_point_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now()).time_since_epoch().count();
}

TimeConsuming::~TimeConsuming() {
    time_t consuming = std::chrono::time_point_cast<std::chrono::milliseconds>(std::chrono::system_clock::from_time_t(_clock)).time_since_epoch().count();
    std::cout << consuming << " ms" << std::endl;
}

}