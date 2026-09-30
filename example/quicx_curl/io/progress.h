#ifndef TOOL_QC_IO_PROGRESS_H
#define TOOL_QC_IO_PROGRESS_H

#include <cstdint>
#include <cstdio>

// Single-line progress meter on stderr (-#). Fed byte counts by the driver;
// throttled redraw keeps CPU cost negligible on fast transfers.
// Not thread-safe across instances; one instance per transfer, driven from
// the single driver callback path.
class Progress {
public:
    Progress(bool enabled, std::FILE* out = stderr);

    void Begin(uint64_t expected_total);
    void Update(uint64_t bytes_so_far);
    void Finish(uint64_t bytes_so_far);

private:
    void Draw(uint64_t bytes);

    bool enabled_;
    std::FILE* out_;
    uint64_t expected_ = 0;
    uint64_t last_drawn_bytes_ = 0;
    bool began_ = false;
};

#endif  // TOOL_QC_IO_PROGRESS_H
