#ifndef COMMON_UTIL_TIME
#define COMMON_UTIL_TIME

#include <cstdint>
#include <string>

namespace quicx {
namespace common {

static const uint8_t kFormatTimeBufSize = sizeof("xxxx-xx-xx:xx:xx:xx:xxx");

enum FormatTimeUnit {
    kYearFormat = 1,         // 2021
    kMonthFormat = 2,        // 2021-03
    kDayFormat = 3,          // 2021-03-16
    kHourFormat = 4,         // 2021-03-16:10
    kMinuteFormat = 5,       // 2021-03-16:10:03
    kSecondFormat = 6,       // 2021-03-16:10:03:33
    kMillisecondFormat = 7,  // 2021-03-16:10:03:33:258
};

enum TimeUnit {
    kMillisecond = 1,
    kSecond = kMillisecond * 1000,
    kMinute = kSecond * 60,
    kHour = kMinute * 60,
    kDay = kHour * 24,
};

// get format time string [xxxx-xx-xx xx:xx:xx]
std::string GetFormatTime(FormatTimeUnit unit = FormatTimeUnit::kMillisecondFormat);
// get format time string as [xxxx-xx-xx xx:xx:xx]
void GetFormatTime(char* buf, uint32_t& len, FormatTimeUnit unit = FormatTimeUnit::kMillisecondFormat);

// get utc time
//
// Wall-clock (std::chrono::system_clock). Use ONLY for values whose meaning is
// "a point in real time" that must survive a process restart or be comparable
// with an external system — e.g. TLS session ticket creation time
// (SSL_SESSION_get_time), certificate validity, log/qlog timestamps.
//
// DO NOT use it for duration or deadline arithmetic: it can step backwards
// (NTP correction, manual settime, VM resume), which turns "now - then" into a
// huge unsigned value and silently breaks timers. Use MonotonicTimeMsec().
uint64_t UTCTimeSec();
uint64_t UTCTimeMsec();

// Monotonic time in milliseconds, from an arbitrary but process-stable epoch
// (std::chrono::steady_clock).
//
// This is the clock every protocol-timing decision must use: RTT sampling,
// PTO, loss detection, idle/closing timeouts, pacing, ACK-delay accounting and
// per-packet send timestamps. It never jumps, so a duration computed from two
// samples is always the elapsed time.
//
// It is NOT comparable across processes and carries no calendar meaning — for
// anything user- or dashboard-visible, use UTCTimeMsec().
uint64_t MonotonicTimeMsec();

// sleep interval milliseconds
void Sleep(uint32_t interval);

}  // namespace common
}  // namespace quicx

#endif  // COMMON_UTIL_TIME