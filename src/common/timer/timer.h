// Use of this source code is governed by a BSD 3-Clause License
// that can be found in the LICENSE file.

// Author: caozhiyi (caozhiyi5@gmail.com)

#ifndef COMMON_TIMER_TIMER
#define COMMON_TIMER_TIMER

#include <memory>
#include "common/timer/timer_interface.h"

namespace quicx {
namespace common {

std::shared_ptr<ITimer> MakeTimer();

}
}

#endif