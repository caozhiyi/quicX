#ifndef COMMON_UTIL_BITMAP
#define COMMON_UTIL_BITMAP

#include <vector>
#include <cstdint>

namespace quicx {

class Bitmap {
public:
    Bitmap();
    ~Bitmap();

    bool Init(uint32_t size);

    bool Insert(uint32_t index);
    bool Remove(uint32_t index);
    // get min index after input param
    // if return -1, mean the bitmap has no value
    int32_t GetMinAfter(uint32_t index = 0);

private:
    std::vector<int64_t> _bitmap;
};

}

#endif