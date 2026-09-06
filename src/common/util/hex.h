#ifndef COMMON_UTIL_HEX
#define COMMON_UTIL_HEX

#include <cstdint>
#include <string>

namespace quicx {
namespace common {

/**
 * @brief Render bytes as a lowercase hex string.
 *
 * Single shared implementation for all byte-dump sites (debug logs, qlog
 * JSON fields, connection-id strings). Table lookup — no snprintf per byte,
 * which matters because the log/qlog paths call this on hot flows.
 *
 * @param data bytes to render; nullptr with len > 0 yields an empty string.
 * @param len  number of bytes to render.
 * @param sep  optional separator inserted BETWEEN bytes (e.g. ' ' for
 *             human-readable logs). '\0' (default) renders the compact
 *             "0a1b2c" form used in qlog/CID strings. No trailing separator.
 */
inline std::string BytesToHex(const uint8_t* data, size_t len, char sep = '\0') {
    static const char kHex[] = "0123456789abcdef";
    if (data == nullptr || len == 0) {
        return std::string();
    }

    const size_t out_len = sep != '\0' ? (len * 3 - 1) : (len * 2);
    std::string out;
    out.reserve(out_len);
    for (size_t i = 0; i < len; ++i) {
        if (sep != '\0' && i != 0) {
            out.push_back(sep);
        }
        out.push_back(kHex[data[i] >> 4]);
        out.push_back(kHex[data[i] & 0x0f]);
    }
    return out;
}

}  // namespace common
}  // namespace quicx

#endif  // COMMON_UTIL_HEX
