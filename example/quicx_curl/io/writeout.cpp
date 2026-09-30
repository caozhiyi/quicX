#include "io/writeout.h"

#include <vector>

namespace {

// Registry: name -> value getter. Adding a variable = one row here.
struct VarRow {
    const char* name;
    std::string (*get)(const WriteoutContext&);
};

std::string S(std::string s) { return s; }
std::string Num(unsigned long long v) { return std::to_string(v); }
std::string Fixed3(double v) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.3f", v);
    return buf;
}
std::string Fixed1(double v) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.1f", v);
    return buf;
}

const std::vector<VarRow>& Registry() {
    static const std::vector<VarRow> kVars = {
        {"url_effective", [](const WriteoutContext& c) { return S(c.url_effective); }},
        {"url", [](const WriteoutContext& c) { return S(c.url_original); }},
        {"http_code", [](const WriteoutContext& c) { return Num(c.http_code); }},
        {"response_code", [](const WriteoutContext& c) { return Num(c.http_code); }},
        {"size_download", [](const WriteoutContext& c) { return Num(c.size_download); }},
        {"size_upload", [](const WriteoutContext& c) { return Num(c.size_upload); }},
        {"time_total", [](const WriteoutContext& c) { return Fixed3(c.time_total); }},
        {"time_starttransfer", [](const WriteoutContext& c) { return Fixed3(c.time_starttransfer); }},
        {"speed_download", [](const WriteoutContext& c) { return Fixed1(c.speed_download); }},
        {"num_redirects", [](const WriteoutContext& c) { return Num(static_cast<unsigned long long>(c.num_redirects)); }},
        {"content_type", [](const WriteoutContext& c) { return S(c.content_type); }},
        {"errormsg", [](const WriteoutContext& c) { return S(c.errormsg); }},
        {"quic_version", [](const WriteoutContext& c) { return S(c.quic_version.empty() ? std::string("N/A") : c.quic_version); }},
        {"quic_rtt", [](const WriteoutContext&) { return S("N/A"); }},
        {"quic_0rtt", [](const WriteoutContext&) { return S("N/A"); }},
        {"quic_loss", [](const WriteoutContext&) { return S("N/A"); }},
    };
    return kVars;
}

bool Resolve(const std::string& name, const WriteoutContext& ctx, std::string& out) {
    for (const auto& row : Registry()) {
        if (name == row.name) {
            out = row.get(ctx);
            return true;
        }
    }
    return false;
}

void EmitEscape(char c, std::FILE* out) {
    switch (c) {
        case 'n': std::fputc('\n', out); break;
        case 't': std::fputc('\t', out); break;
        case 'r': std::fputc('\r', out); break;
        default: std::fputc(c, out); break;  // unknown escape: keep as-is
    }
}

}  // namespace

void WriteOut(const std::string& fmt, const WriteoutContext& ctx, std::FILE* out) {
    for (size_t i = 0; i < fmt.size(); ++i) {
        if (fmt[i] == '\\' && i + 1 < fmt.size()) {
            EmitEscape(fmt[++i], out);
            continue;
        }
        if (fmt[i] == '%' && i + 1 < fmt.size() && fmt[i + 1] == '{') {
            size_t close = fmt.find('}', i + 2);
            if (close == std::string::npos) {
                std::fputc(fmt[i], out);  // unterminated %{ : literal
                continue;
            }
            std::string name = fmt.substr(i + 2, close - i - 2);
            std::string value;
            if (Resolve(name, ctx, value)) {
                std::fwrite(value.data(), 1, value.size(), out);
            } else {
                std::fprintf(out, "%%{%s}", name.c_str());  // unknown: verbatim
            }
            i = close;
            continue;
        }
        std::fputc(fmt[i], out);
    }
    std::fflush(out);
}
