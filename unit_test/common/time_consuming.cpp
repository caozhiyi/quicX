#include <iostream>
#include "time_consuming.h"

namespace quicx {
namespace common {

TimeConsuming::TimeConsuming(std::string name) : name_(name),
    start_time_(std::chrono::system_clock::now()) {
}

TimeConsuming::~TimeConsuming() {
    std::chrono::milliseconds time_span = std::chrono::duration_cast<std::chrono::milliseconds>  \
        (std::chrono::system_clock::now() - start_time_);

    std::cout << name_ << " used " << time_span.count() << " ms." << std::endl;
}

}
}