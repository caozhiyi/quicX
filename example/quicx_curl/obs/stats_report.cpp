#include "obs/stats_report.h"

void PrintStats(std::FILE* out, const StatsInput& s) {
    double total = s.timing ? s.timing->TotalSeconds() : 0.0;
    double ttfb = s.timing ? s.timing->TtfbSeconds() : 0.0;
    double speed = total > 0 ? static_cast<double>(s.bytes_downloaded) / total : 0.0;

    std::fprintf(out, "===== transfer stats =====\n");
    std::fprintf(out, "%-24s %u\n", "http status:", s.status_code);
    std::fprintf(out, "%-24s %d\n", "redirect hops:", s.redirect_hops);
    std::fprintf(out, "%-24s %.3f s\n", "time_starttransfer:", ttfb);
    std::fprintf(out, "%-24s %.3f s\n", "time_total:", total);
    std::fprintf(out, "%-24s %llu bytes\n", "size_download:",
        static_cast<unsigned long long>(s.bytes_downloaded));
    std::fprintf(out, "%-24s %.1f bytes/s\n", "speed_download:", speed);
    std::fprintf(out, "%-24s %s\n", "quic_version:", s.quic_version.c_str());
    std::fprintf(out, "%-24s %s\n", "0-rtt:", s.zero_rtt_requested ? "requested" : "off");
    std::fprintf(out, "%-24s %s\n", "quic_rtt:", "N/A (not exported by stack)");
    std::fprintf(out, "%-24s %s\n", "quic_loss:", "N/A (not exported by stack)");
    std::fprintf(out, "%-24s %s\n", "quic_cwnd:", "N/A (not exported by stack)");
    std::fprintf(out, "==========================\n");
}
