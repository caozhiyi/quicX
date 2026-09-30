#ifndef TOOL_QC_OBS_TRACER_H
#define TOOL_QC_OBS_TRACER_H

#include <cstdarg>
#include <cstdio>
#include <string>

// Verbus diagnostic channel (stderr, curl "* "-prefixed style).
// Diagnostic output NEVER mixes into the body channel, so stdout stays
// binary-safe for piping.
class Tracer {
public:
    Tracer() = default;
    Tracer(bool verbose, bool show_error) : verbose_(verbose), show_error_(show_error) {}

    void SetVerbose(bool v) { verbose_ = v; }

    // curl-style info line: "* ..." on stderr when -v is given.
    void Info(const char* fmt, ...) const {
        if (!verbose_) return;
        va_list ap;
        va_start(ap, fmt);
        std::fprintf(stderr, "* ");
        std::vfprintf(stderr, fmt, ap);
        std::fprintf(stderr, "\n");
        va_end(ap);
    }

    // Error line: shown unless fully silenced (-s without -S).
    void Error(const char* fmt, ...) const {
        if (!show_error_) return;
        va_list ap;
        va_start(ap, fmt);
        std::fprintf(stderr, "quicx-curl: ");
        std::vfprintf(stderr, fmt, ap);
        std::fprintf(stderr, "\n");
        va_end(ap);
    }

private:
    bool verbose_ = false;
    bool show_error_ = true;
};

#endif  // TOOL_QC_OBS_TRACER_H
