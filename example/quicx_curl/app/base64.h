#ifndef TOOL_QC_APP_BASE64_H
#define TOOL_QC_APP_BASE64_H

#include <cstdint>
#include <string>

// Standard base64 encode (RFC 4648) for HTTP Basic auth. Decode not needed.
inline std::string Base64Encode(const std::string& in) {
    static const char kTable[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((in.size() + 2) / 3 * 4);
    uint32_t buf = 0;
    int bits = 0;
    for (unsigned char c : in) {
        buf = (buf << 8) | c;
        bits += 8;
        if (bits == 24) {
            out += kTable[(buf >> 18) & 0x3F];
            out += kTable[(buf >> 12) & 0x3F];
            out += kTable[(buf >> 6) & 0x3F];
            out += kTable[buf & 0x3F];
            bits = 0;
            buf = 0;
        }
    }
    if (bits == 8) {
        buf <<= 16;
        out += kTable[(buf >> 18) & 0x3F];
        out += kTable[(buf >> 12) & 0x3F];
        out += "==";
    } else if (bits == 16) {
        buf <<= 8;
        out += kTable[(buf >> 18) & 0x3F];
        out += kTable[(buf >> 12) & 0x3F];
        out += kTable[(buf >> 6) & 0x3F];
        out += '=';
    }
    return out;
}

#endif  // TOOL_QC_APP_BASE64_H
