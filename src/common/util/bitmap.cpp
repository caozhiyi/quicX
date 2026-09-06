#include <cmath>

#include "common/util/bitmap.h"

namespace quicx {
namespace common {

static const uint32_t kStepSize = sizeof(int64_t) * 8;
static const uint64_t kSetpBase = 1;

namespace {
// Index of the lowest set bit. Isolating it with x & -x yields an exact power of two,
// which float represents without rounding, so log2f is safe up to 2^63 here.
// Callers must reject zero first: log2f(0) is -inf and converting that to an unsigned
// index is undefined behaviour.
uint32_t LowestSetBitIndex(uint64_t bits) {
    return (uint32_t)std::log2f(float(bits & (~bits + 1)));
}
}  // namespace

Bitmap::Bitmap():
    vec_bitmap_(0) {}

Bitmap::~Bitmap() {}

bool Bitmap::Init(uint32_t size) {
    uint32_t vec_size = size / kStepSize;
    // too large size
    if (vec_size > sizeof(vec_bitmap_) * 8) {
        return false;
    }
    if (size % kStepSize > 0) {
        vec_size++;
    }
    bitmap_.resize(vec_size);
    for (std::size_t i = 0; i < bitmap_.size(); i++) {
        bitmap_[i] = 0;
    }
    return true;
}

bool Bitmap::Insert(uint32_t index) {
    if (index > bitmap_.size() * kStepSize) {
        return false;
    }

    // get index in vector
    uint32_t bitmap_index = index / kStepSize;
    // get index in uint64_t
    uint32_t bit_index = index % kStepSize;

    bitmap_[bitmap_index] |= kSetpBase << bit_index;
    vec_bitmap_ |= kSetpBase << bitmap_index;

    return true;
}

bool Bitmap::Remove(uint32_t index) {
    if (index > bitmap_.size() * kStepSize) {
        return false;
    }

    // get index in vector
    uint32_t bitmap_index = index / kStepSize;
    // get index in uint64_t
    uint32_t bit_index = index % kStepSize;

    bitmap_[bitmap_index] &= ~(kSetpBase << bit_index);
    if (bitmap_[bitmap_index] == 0) {
        vec_bitmap_ &= ~(kSetpBase << bitmap_index);
    }
    return true;
}

int32_t Bitmap::GetMinAfter(uint32_t index) {
    // get next bit.
    if (index >= bitmap_.size() * kStepSize || Empty()) {
        return -1;
    }

    // get index in vector
    uint32_t bitmap_index = index / kStepSize;
    // filter smaller bitmap index
    uint32_t ret = bitmap_index * kStepSize;

    // find current uint64_t have next 1?
    if (bitmap_[bitmap_index] != 0) {
        // Unsigned: an arithmetic right shift of a word with bit 63 set would smear
        // sign bits into the high end and make the "no more bits" test unreliable.
        uint64_t cur_bitmap = (uint64_t)bitmap_[bitmap_index];
        uint32_t cur_step = index - ret;
        cur_bitmap = cur_bitmap >> cur_step;

        // don't have next 1
        if (cur_bitmap == 0) {
            ret += kStepSize;

            // find next 1
        } else {
            ret += cur_step;
            ret += LowestSetBitIndex(cur_bitmap);
            return ret;
        }

    } else {
        ret += kStepSize;
    }

    // Find the next used vector. Bit 0 refers to the current vector, which either has
    // no bits at all or only bits at/before `index` (both rejected above), so mask it
    // off: leaving it in would make the scan resolve back to the current vector.
    uint32_t temp_vec_bitmap = (vec_bitmap_ >> bitmap_index) & ~1u;
    if (temp_vec_bitmap == 0) {
        return -1;
    }

    // Guaranteed >= 1 because bit 0 was masked off above.
    uint32_t next_vec_index = LowestSetBitIndex(temp_vec_bitmap);
    uint32_t target_vec_index = next_vec_index + bitmap_index;

    // Here we want the genuine lowest set bit of the target vector, including bit 0.
    uint64_t cur_bitmap = (uint64_t)bitmap_[target_vec_index];
    ret += (next_vec_index - 1) * kStepSize;
    ret += LowestSetBitIndex(cur_bitmap);

    return ret;
}

bool Bitmap::Empty() {
    return vec_bitmap_ == 0;
}

void Bitmap::Clear() {
    while (vec_bitmap_ != 0) {
        // The true lowest set bit is wanted here: every flagged vector must be cleared,
        // including vector 0.
        uint32_t next_vec_index = LowestSetBitIndex(vec_bitmap_);
        bitmap_[next_vec_index] = 0;
        vec_bitmap_ = vec_bitmap_ & (vec_bitmap_ - 1);
    }
}

}  // namespace common
}  // namespace quicx