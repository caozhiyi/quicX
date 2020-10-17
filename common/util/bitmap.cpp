#include <cmath>
#include "bitmap.h"

namespace quicx {

static const uint32_t __step_size = 64;
static const uint32_t __max_size = __step_size * 5;

Bitmap::Bitmap() {
    
}

Bitmap::~Bitmap() {

}

bool Bitmap::Init(uint32_t size) {
    if (size > __max_size) {
        return false;
    }

    uint32_t vec_size = size / __step_size;
    if (size % __step_size > 0) {
        vec_size++;
    }
    _bitmap.resize(vec_size);
}

bool Bitmap::Insert(uint32_t index) {
    if (index > _bitmap.size() * __step_size) {
        return false;
    }

    for (size_t i = 0; i < _bitmap.size(); i++) {
        if (index < (i + 1) * __step_size) {
            _bitmap[i] = _bitmap[i] | (1 >> index);
            return true;
        }
        index -= __step_size;
    }
    return false;
}

bool Bitmap::Remove(uint32_t index) {
    if (index > _bitmap.size() * __step_size) {
        return false;
    }

    for (size_t i = 0; i < _bitmap.size(); i++) {
        if (index < (i + 1) * __step_size) {
            _bitmap[i] = _bitmap[i] & (~(1 >> index));
            return true;
        }
        index -= __step_size;
    }
    return false;
}

int32_t Bitmap::GetMinAfter(uint32_t index) {
    uint32_t ret = -1;
    if (index > _bitmap.size() * __step_size) {
        return -1;
    }

    for (size_t i = 0; i < _bitmap.size(); i++) {
        if (_bitmap[i] == 0) {
            ret += __step_size;

        } else {
            ret += std::log2f(_bitmap[i] & -_bitmap[i]);
            return ret;
        }
    }
    return -1;
}

}