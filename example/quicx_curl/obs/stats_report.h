#ifndef TOOL_QC_OBS_STATS_REPORT_H
#define TOOL_QC_OBS_STATS_REPORT_H

#include <cstdint>
#include <cstdio>
#include <string>

#include "timing.h"

// --stats summary block on stderr after a transfer. QUIC-level counters
// (rtt / loss / 0-rtt state) are not yet exported by the library; they are
// reported as N/A until a stats interface lands, instead of being faked.
struct StatsInput {
    const Timing* timing = nullptr;
    uint64_t bytes_downloaded = 0;
    uint64_t bytes_uploaded = 0;  // unknown until provider reports; 0 = none
    uint32_t status_code = 0;
    int redirect_hops = 0;
    std::string quic_version;     // negotiated-by-config value ("v1"/"v2")
    bool zero_rtt_requested = false;
};

void PrintStats(std::FILE* out, const StatsInput& s);

#endif  // TOOL_QC_OBS_STATS_REPORT_H
