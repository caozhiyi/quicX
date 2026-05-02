#ifndef COMMON_UTIL_OS_RETURN
#define COMMON_UTIL_OS_RETURN

#include <cstdint>

namespace quicx {
namespace common {

template <typename T>
struct SysCallResult {
  T return_value_;
  int32_t error_code_;  // Renamed from errno_ to avoid conflict with POSIX errno macro
};

using SysCallInt32Result = SysCallResult<int32_t>;

}
}

#endif
