#ifndef COMMON_LOG_LOG_CONTEXT
#define COMMON_LOG_LOG_CONTEXT

#include <cstdint>
#include <cstring>
#include <string>

namespace quicx {
namespace common {

class LogContext {
public:
    // Append a tag to the current context.
    // Recommended to pass a pre-formatted string like "[conn:123]"
    static void Append(const char* tag, size_t len);

    // Truncate the context to a specific length.
    // Used to restore the context state (pop).
    static void Truncate(size_t len);

    // Get the current length of the context.
    static size_t Size();

    // Get the full context string.
    static const char* GetTag();
};

// RAII Guard for automatically appending and removing tags
class LogTagGuard {
public:
    explicit LogTagGuard(const std::string& tag) {
        old_len_ = LogContext::Size();
        LogContext::Append(tag.c_str(), tag.length());
    }

    // Overload for c-string to avoid std::string construction if not needed
    LogTagGuard(const char* tag) {
        old_len_ = LogContext::Size();
        LogContext::Append(tag, strlen(tag));
    }

    // Zero-allocation numeric tag: formats "<prefix><value>" into a stack
    // buffer. Replaces the hot-path spelling
    //     LogTagGuard guard("|strm:" + std::to_string(id));
    // which cost two heap allocations (to_string temp + operator+ result)
    // plus a memcpy per guard — and guards sit on the per-packet,
    // per-stream path (perf: the single largest memmove caller inside
    // StreamManager::BuildStreamFrames). The template array parameter
    // binds only to string literals, so the prefix length is a compile-time
    // constant and no strlen is needed.
    template <size_t N>
    LogTagGuard(const char (&prefix)[N], uint64_t value) {
        char buf[N + kMaxUint64Digits];  // prefix (minus NUL) + decimal digits
        size_t len = N - 1;
        std::memcpy(buf, prefix, len);
        len += FormatUint64(buf + len, value);
        old_len_ = LogContext::Size();
        LogContext::Append(buf, len);
    }

    ~LogTagGuard() { LogContext::Truncate(old_len_); }

private:
    static constexpr size_t kMaxUint64Digits = 20;  // strlen("18446744073709551615")

    static size_t FormatUint64(char* out, uint64_t v) {
        char tmp[kMaxUint64Digits];
        size_t n = 0;
        do {
            tmp[n++] = static_cast<char>('0' + (v % 10));
            v /= 10;
        } while (v != 0);
        for (size_t i = 0; i < n; ++i) {
            out[i] = tmp[n - 1 - i];
        }
        return n;
    }

    size_t old_len_;
};

}  // namespace common
}  // namespace quicx

#endif  // COMMON_LOG_LOG_CONTEXT
