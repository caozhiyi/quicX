#include "io/progress.h"

#include <chrono>
#include <cstring>

namespace {
constexpr uint64_t kRedrawIntervalBytes = 64 * 1024;  // redraw every 64 KiB
constexpr int kBarWidth = 40;
}  // namespace

Progress::Progress(bool enabled, std::FILE* out) : enabled_(enabled), out_(out) {}

void Progress::Begin(uint64_t expected_total) {
    expected_ = expected_total;
    last_drawn_bytes_ = 0;
    began_ = true;
    if (!enabled_) return;
    if (expected_ > 0)
        std::fprintf(out_, "%llu %llu\n",
            static_cast<unsigned long long>(0), static_cast<unsigned long long>(expected_));
    Draw(0);
}

void Progress::Update(uint64_t bytes) {
    if (!enabled_ || !began_) return;
    if (expected_ > 0 && bytes < expected_ && bytes - last_drawn_bytes_ < kRedrawIntervalBytes) return;
    if (expected_ == 0 && bytes - last_drawn_bytes_ < kRedrawIntervalBytes) return;
    Draw(bytes);
}

void Progress::Finish(uint64_t bytes) {
    if (!enabled_ || !began_) return;
    Draw(bytes);
    if (expected_ > 0)
        std::fprintf(out_, "%llu %llu\n",
            static_cast<unsigned long long>(bytes), static_cast<unsigned long long>(expected_));
    std::fflush(out_);
    began_ = false;
}

void Progress::Draw(uint64_t bytes) {
    last_drawn_bytes_ = bytes;
    if (expected_ > 0) {
        double frac = bytes >= expected_ ? 1.0 : static_cast<double>(bytes) / static_cast<double>(expected_);
        int filled = static_cast<int>(frac * kBarWidth);
        std::fprintf(out_, "\r%3.0f%% [", frac * 100.0);
        for (int i = 0; i < kBarWidth; ++i) std::fputc(i < filled ? '#' : ' ', out_);
        std::fprintf(out_, "] %llu/%llu bytes",
            static_cast<unsigned long long>(bytes), static_cast<unsigned long long>(expected_));
    } else {
        std::fprintf(out_, "\rReceived: %llu bytes", static_cast<unsigned long long>(bytes));
    }
    std::fflush(out_);
}
