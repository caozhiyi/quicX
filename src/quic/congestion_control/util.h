#ifndef QUIC_CONGESTION_CONTROL_UTIL
#define QUIC_CONGESTION_CONTROL_UTIL

#include <cstdint>
#include <algorithm>
#include <stdexcept>

namespace quicx {
namespace quic {
namespace congestion_control {

// Safe multiplication: returns true if overflow occurred
inline bool multiply_overflow(uint64_t a, uint64_t b, uint64_t& result) {
    if (a == 0 || b == 0) {
        result = 0;
        return false;
    }
    
    // Check for overflow: if a > UINT64_MAX / b, then a * b > UINT64_MAX
    if (a > UINT64_MAX / b) {
        return true; // Overflow
    }
    
    result = a * b;
    return false;
}

// Safe multiplication with saturation (clamp to UINT64_MAX on overflow)
inline uint64_t multiply_saturate(uint64_t a, uint64_t b) {
    uint64_t result;
    if (multiply_overflow(a, b, result)) {
        return UINT64_MAX;
    }
    return result;
}

// Safe division with overflow protection
inline uint64_t divide_safe(uint64_t a, uint64_t b) {
    if (b == 0) {
        return UINT64_MAX; // Return max value on division by zero
    }
    return a / b;
}

// MulDiv equivalent that replaces __int128 usage
// Returns (a * num) / den with overflow protection
inline uint64_t muldiv_safe(uint64_t a, uint64_t num, uint64_t den) {
    if (den == 0) {
        return 0;
    }
    
    // First check if we can do the multiplication safely
    uint64_t product;
    if (multiply_overflow(a, num, product)) {
        // Overflow occurred, use alternative approach
        // For large numbers, we can do the division first to reduce magnitude
        if (a > den) {
            uint64_t quotient = a / den;
            uint64_t remainder = a % den;
            // (quotient * den + remainder) * num / den = quotient * num + (remainder * num) / den
            uint64_t first_part = multiply_saturate(quotient, num);
            uint64_t second_part = muldiv_safe(remainder, num, den);
            return std::min(UINT64_MAX, first_part + second_part);
        } else if (num > den) {
            uint64_t quotient = num / den;
            uint64_t remainder = num % den;
            // a * (quotient * den + remainder) / den = a * quotient + (a * remainder) / den
            uint64_t first_part = multiply_saturate(a, quotient);
            uint64_t second_part = muldiv_safe(a, remainder, den);
            return std::min(UINT64_MAX, first_part + second_part);
        } else {
            // Both a and num are smaller than den, but product overflows
            // This is a rare case, use saturation
            return UINT64_MAX;
        }
    }
    
    return product / den;
}

}

} // namespace quic
} // namespace quicx

#endif