#ifndef COMMON_UTIL_OS_RETURN
#define COMMON_UTIL_OS_RETURN

namespace quicx {

template <typename T>
struct SysCallResult {
  T _return_value;
  int32_t errno_;
};

using SysCallIntResult = SysCallResult<int>;

}

#endif