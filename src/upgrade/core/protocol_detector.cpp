#include <array>
#include <cctype>
#include <cstring>

#include "upgrade/core/protocol_detector.h"

namespace quicx {
namespace upgrade {

Protocol ProtocolDetector::Detect(const std::vector<uint8_t>& data) {
    if (data.empty()) {
        return Protocol::UNKNOWN;
    }
    
    // Check if it's an HTTP/2 connection
    if (IsHTTP2(data)) {
        return Protocol::HTTP2;
    }
    
    // Check if it's an HTTP/1.1 connection
    if (IsHTTP1_1(data)) {
        return Protocol::HTTP1_1;
    }
    
    return Protocol::UNKNOWN;
}

bool ProtocolDetector::IsHTTP1_1(const std::vector<uint8_t>& data) {
    if (data.size() < 4) {
        return false;
    }

    // Find the first CRLF in the data
    static auto find_crlf = [](std::vector<uint8_t>::const_iterator from, std::vector<uint8_t>::const_iterator data_end) {
        for (auto p = from; p != data_end; ++p) {
            if (*p == '\r' && (p + 1) != data_end && *(p + 1) == '\n') {
                return p; // points to '\r'
            }
        }
        return data_end;
    };

    // Require end of headers (CRLF [space/tab]* CRLF) to avoid false positives on partial data
    auto it = data.begin();
    auto data_end = data.end();
    auto first_crlf = find_crlf(it, data_end);
    // If no CRLF found, return false
    if (first_crlf == data_end) {
        return false;
    }

    auto after_first = first_crlf + 2;
    // Skip optional spaces/tabs forming an "empty" header line
    auto p = after_first;
    while (p != data_end && (*p == ' ' || *p == '\t')) {
        ++p;
    }

    // Find the second CRLF in the data
    auto second_crlf = find_crlf(p, data_end);
    if (second_crlf == data_end) {
        return false;
    }

    // Define end iterator as end of headers marker
    auto it_end = second_crlf;

    // Build a lowercase prefix without leading spaces for robust matching
    size_t scan_len = std::min(static_cast<size_t>(64), static_cast<size_t>(std::distance(data.begin(), it_end)));
    std::string prefix(data.begin(), data.begin() + scan_len);

    // Trim leading spaces
    size_t start = 0;
    while (start < prefix.size() && std::isspace(static_cast<unsigned char>(prefix[start]))) {
        start++;
    }

    // Convert to lowercase
    std::string lowered = prefix.substr(start);
    for (size_t i = 0; i < lowered.size(); ++i) {
        lowered[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(lowered[i])));
    }

    // Methods (lowercase) followed by space
    static const char* methods[] = {"get ", "post ", "put ", "delete ", "head ", "options "};
    for (const char* m : methods) {
        size_t mlen = std::strlen(m);
        if (lowered.compare(0, mlen, m) == 0) {
            // Also require HTTP/1.1 appears on the request line
            size_t crlf = lowered.find("\r\n");
            std::string line = (crlf == std::string::npos) ? lowered : lowered.substr(0, crlf);
            return line.find("http/1.1") != std::string::npos;
        }
    }

    // HTTP response line (case-insensitive)
    return lowered.rfind("http/", 0) == 0;
}

bool ProtocolDetector::IsHTTP2(const std::vector<uint8_t>& data) {
    // Detect HTTP/2 connection preface: "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n"
    if (data.size() >= 24) {
        static const std::array<uint8_t, 24> http2_preface = {
            'P','R','I',' ','*',' ','H','T','T','P','/','2','.','0','\r','\n','\r','\n','S','M','\r','\n','\r','\n'
        };
        if (std::equal(http2_preface.begin(), http2_preface.end(), data.begin())) {
            return true;
        }
    }

    // Detect a valid HTTP/2 frame header (e.g., SETTINGS) at start
    if (data.size() >= 9) {
        uint32_t length = (static_cast<uint32_t>(data[0]) << 16) |
                          (static_cast<uint32_t>(data[1]) << 8) |
                          (static_cast<uint32_t>(data[2]));
        uint8_t type = data[3];
        uint8_t /*flags*/ _flags = data[4];
        uint32_t stream_id = (static_cast<uint32_t>(data[5]) << 24) |
                             (static_cast<uint32_t>(data[6]) << 16) |
                             (static_cast<uint32_t>(data[7]) << 8) |
                             (static_cast<uint32_t>(data[8]));
        bool reserved_bit_set = (stream_id & 0x80000000u) != 0;
        stream_id &= 0x7FFFFFFFu;

        if (!reserved_bit_set && data.size() == static_cast<size_t>(9 + length)) {
            // SETTINGS must be on stream 0
            if (type == 0x04 && stream_id == 0) {
                return true;
            }
            // Other valid frame types on stream 0 at start could also indicate HTTP/2
            // e.g., WINDOW_UPDATE (0x08) on non-zero stream, PING(0x06) on stream 0, etc.
            if ((type == 0x06 /*PING*/ && stream_id == 0) ||
                (type == 0x03 /*RST_STREAM*/ && stream_id != 0) ||
                (type == 0x08 /*WINDOW_UPDATE*/)) {
                return true;
            }
        }
    }

    return false;
}

} // namespace upgrade
} // namespace quicx 