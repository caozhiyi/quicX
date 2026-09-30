#ifndef TOOL_QC_IO_WRITEOUT_H
#define TOOL_QC_IO_WRITEOUT_H

#include <cstdint>
#include <cstdio>
#include <string>

// -w template engine. Variables are resolved from a plain context struct so
// io/ stays free of library headers. Unknown %{name} is emitted verbatim
// (curl-compatible enough for scripts to notice).
struct WriteoutContext {
    std::string url_effective;
    std::string url_original;
    uint32_t http_code = 0;
    uint64_t size_download = 0;
    uint64_t size_upload = 0;
    double time_total = 0;
    double time_starttransfer = 0;
    double speed_download = 0;  // bytes/sec
    int num_redirects = 0;
    std::string content_type;
    std::string errormsg;
    // quic_ extensions (currently config-derived; stack export pending)
    std::string quic_version;
};

// Renders `fmt` writing plain text to `out`. Supports %{name}, \n, \t, \r.
void WriteOut(const std::string& fmt, const WriteoutContext& ctx, std::FILE* out);

#endif  // TOOL_QC_IO_WRITEOUT_H
