#ifndef HTTP3_QPACK_UTIL
#define HTTP3_QPACK_UTIL

#include <utility>
#include <functional>

namespace quicx {
namespace http3 {

// Custom hash function for std::pair
struct pair_hash {
    template <class T1, class T2>
    std::size_t operator () (const std::pair<T1,T2> &pair) const {
        auto hash1 = std::hash<T1>{}(pair.first);
        auto hash2 = std::hash<T2>{}(pair.second);
        return hash1 ^ hash2;
    }
};

}
}

#endif
