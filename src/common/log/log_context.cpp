#include "common/config.h"
#include "common/log/log_context.h"

namespace quicx {
namespace common {

// Use a thread_local string as the buffer.
// std::string handles memory management and we can reserve space to avoid allocations.
thread_local std::string g_log_context_buffer;
thread_local bool g_buffer_initialized = false;

static void EnsureInit() {
    if (!g_buffer_initialized) {
        g_log_context_buffer.reserve(kDefaultContextSize);
        g_buffer_initialized = true;
    }
}

void LogContext::Append(const char* tag, size_t len) {
    EnsureInit();
    g_log_context_buffer.append(tag, len);
}

void LogContext::Truncate(size_t len) {
    if (len < g_log_context_buffer.length()) {
        g_log_context_buffer.resize(len);
    }
}

size_t LogContext::Size() {
    return g_log_context_buffer.length();
}

const char* LogContext::GetTag() {
    return g_log_context_buffer.c_str();
}

}  // namespace common
}  // namespace quicx
