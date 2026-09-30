#ifndef TOOL_QC_OBS_TIMING_H
#define TOOL_QC_OBS_TIMING_H

#include <chrono>

// Per-request phase timing. All clock reads use steady_clock (monotonic).
// The QUIC stack currently exposes no connection-state callback through
// IClient, so connect/handshake phases are approximated as unavailable;
// ttfb covers handshake + server processing as curl's time_starttransfer does.
class Timing {
public:
    void Start() {
        start_ = std::chrono::steady_clock::now();
        has_first_byte_ = false;
        has_end_ = false;
    }
    void MarkFirstByte() {
        if (!has_first_byte_) first_byte_ = std::chrono::steady_clock::now();
        has_first_byte_ = true;
    }
    void MarkDone() {
        if (!has_end_) end_ = std::chrono::steady_clock::now();
        has_end_ = true;
    }

    double TtfbSeconds() const {
        auto t = has_first_byte_ ? first_byte_ : Now();
        return Secs(start_, t);
    }
    double TotalSeconds() const {
        auto t = has_end_ ? end_ : Now();
        return Secs(start_, t);
    }

private:
    static std::chrono::steady_clock::time_point Now() { return std::chrono::steady_clock::now(); }
    static double Secs(std::chrono::steady_clock::time_point a, std::chrono::steady_clock::time_point b) {
        return std::chrono::duration<double>(b - a).count();
    }

    std::chrono::steady_clock::time_point start_;
    std::chrono::steady_clock::time_point first_byte_;
    std::chrono::steady_clock::time_point end_;
    bool has_first_byte_ = false;
    bool has_end_ = false;
};

#endif  // TOOL_QC_OBS_TIMING_H
