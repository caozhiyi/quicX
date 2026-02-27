#ifndef COMMON_TIMER_TIMER
#define COMMON_TIMER_TIMER

#include <memory>
#include "common/timer/if_timer.h"

namespace quicx {
namespace common {

std::shared_ptr<ITimer> MakeTimer();

}
}

#endif