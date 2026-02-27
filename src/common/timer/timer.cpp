
#include "common/timer/timer.h"
#include "common/timer/timing_wheel_timer.h"

namespace quicx {
namespace common {

std::shared_ptr<ITimer> MakeTimer() {
    return std::make_shared<TimingWheelTimer>();
}

}
}