
// Use of this source code is governed by a BSD 3-Clause License
// that can be found in the LICENSE file.

// Author: caozhiyi (caozhiyi5@gmail.com)

#include "common/timer/timer.h"
#include "common/timer/treemap_timer.h"

namespace quicx {

std::shared_ptr<ITimer> MakeTimer() {
    return std::make_shared<TreeMapTimer>();
}

}