#include "cli/option_table.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

// ---- setters keep the registry rows one-line -------------------------------
bool SetFlag(bool& field, const std::string& value, Options&) {
    if (!value.empty() && value != "true" && value != "1") return false;
    field = true;
    return true;
}
using Setter = std::function<bool(const std::string&, Options&)>;

Setter SetDouble(double& field, double lo, double hi) {
    return [&field, lo, hi](const std::string& v, Options&) {
        char* end = nullptr;
        double d = std::strtod(v.c_str(), &end);
        if (end == v.c_str() || d < lo || d > hi) return false;
        field = d;
        return true;
    };
}
Setter SetUint(uint32_t& field, uint32_t lo, uint32_t hi) {
    return [&field, lo, hi](const std::string& v, Options&) {
        char* end = nullptr;
        unsigned long long n = std::strtoull(v.c_str(), &end, 10);
        if (end == v.c_str() || n < lo || n > hi) return false;
        field = static_cast<uint32_t>(n);
        return true;
    };
}
Setter SetInt(int& field, int lo, int hi) {
    return [&field, lo, hi](const std::string& v, Options&) {
        char* end = nullptr;
        long n = std::strtol(v.c_str(), &end, 10);
        if (end == v.c_str() || n < lo || n > hi) return false;
        field = static_cast<int>(n);
        return true;
    };
}

bool ParseVersion(const std::string& v, Options& o) {
    if (v == "v1" || v == "1") { o.tp.quic_version = "v1"; return true; }
    if (v == "v2" || v == "2") { o.tp.quic_version = "v2"; return true; }
    return false;
}
bool ParseUserAgent(const std::string& v, Options& o) { o.req.user_agent = v; return true; }

}  // namespace

OptionTable::OptionTable() {
    specs_ = {
        {'X', "request", "http", true, "<method>", "HTTP method (GET/POST/PUT/DELETE/...)",
            [this](const std::string& v, Options& o) { o.req.method = v; return true; }},
        {'H', "header", "http", true, "<header>", "Add request header \"Name: value\" (repeatable)",
            [this](const std::string& v, Options& o) { o.req.headers.push_back(v); return true; }},
        {'d', "data", "http", true, "<data|@file>", "Request body; @file reads from file",
            [this](const std::string& v, Options& o) {
                if (v.size() > 1 && v[0] == '@') o.req.data_file = v.substr(1);
                else o.req.data = v;
                if (o.req.method == "GET") o.req.method = "POST";
                return true;
            }},
        {'T', "upload-file", "http", true, "<file>", "Upload file (streamed, no full buffering)",
            [this](const std::string& v, Options& o) { o.req.upload_file = v; return true; }},
        {'I', "head", "http", false, "", "HEAD method: show headers only",
            [this](const std::string& v, Options& o) { return SetFlag(o.req.head, v, o); }},
        {'A', "user-agent", "http", true, "<name>", "User-Agent header",
            [this](const std::string& v, Options& o) { return ParseUserAgent(v, o); }},
        {'e', "referer", "http", true, "<url>", "Referer header",
            [this](const std::string& v, Options& o) { o.req.referer = v; return true; }},
        {'u', "user", "http", true, "<user:pass>", "Basic auth credentials",
            [this](const std::string& v, Options& o) { o.req.user = v; return true; }},
        {'L', "location", "http", false, "", "Follow 3xx redirects",
            [this](const std::string& v, Options& o) { return SetFlag(o.req.follow_redirect, v, o); }},
        {0, "max-redirs", "http", true, "<num>", "Max redirect hops (default 50)",
            [this](const std::string& v, Options& o) { return SetInt(o.req.max_redirects, 0, 1000)(v, o); }},
        {'b', "cookie", "http", true, "<data|file>", "Send cookies from string or Netscape file",
            [this](const std::string& v, Options& o) { o.req.cookie_read_file = v; return true; }},
        {'c', "cookie-jar", "http", true, "<file>", "Write cookies to Netscape jar after request",
            [this](const std::string& v, Options& o) { o.req.cookie_write_file = v; return true; }},

        // ---- transport group ----
        {'k', "insecure", "transport", false, "", "Skip TLS certificate verification",
            [this](const std::string& v, Options& o) { return SetFlag(o.tp.insecure, v, o); }},
        {0, "cacert", "transport", true, "<file>", "CA bundle for peer verification",
            [this](const std::string& v, Options& o) { o.tp.cacert = v; return true; }},
        {0, "connect-timeout", "transport", true, "<sec>", "Connection timeout in seconds",
            [this](const std::string& v, Options& o) {
                return SetDouble(o.req.connect_timeout_s, 0, 86400)(v, o); }},
        {0, "max-time", "transport", true, "<sec>", "Whole-operation timeout in seconds",
            [this](const std::string& v, Options& o) { return SetDouble(o.req.max_time_s, 0, 86400)(v, o); }},

        // ---- h3 group (QUIC/HTTP3 specific) ----
        {0, "quic-version", "h3", true, "v1|v2", "QUIC version (RFC 9000 v1 / RFC 9369 v2)",
            [this](const std::string& v, Options& o) { return ParseVersion(v, o); }},
        {0, "0rtt", "h3", false, "", "Enable 0-RTT early data with session resumption",
            [this](const std::string& v, Options& o) {
                if (!SetFlag(o.tp.zero_rtt, v, o)) return false;
                if (o.tp.session_cache.empty()) o.tp.session_cache = "./session_cache";
                return true;
            }},
        {0, "session-cache", "h3", true, "<dir>", "TLS session cache dir for resumption",
            [this](const std::string& v, Options& o) { o.tp.session_cache = v; return true; }},
        {0, "qlog", "h3", true, "<dir>", "Write qlog traces to dir (RFC 9284)",
            [this](const std::string& v, Options& o) { o.tp.qlog_dir = v; return true; }},
        {0, "keylog", "h3", true, "<file>", "Write TLS secrets for Wireshark decryption",
            [this](const std::string& v, Options& o) { o.tp.keylog_file = v; return true; }},
        {0, "ecn", "h3", false, "", "Enable ECN (Explicit Congestion Notification)",
            [this](const std::string& v, Options& o) { return SetFlag(o.tp.ecn, v, o); }},
        {0, "key-update", "h3", false, "", "Enable automatic Key Update during connection",
            [this](const std::string& v, Options& o) { return SetFlag(o.tp.key_update, v, o); }},
        {0, "keep-alive", "h3", true, "<ms>", "QUIC keep-alive PING interval (0 = off)",
            [this](const std::string& v, Options& o) { return SetUint(o.tp.keep_alive_ms, 0, 3600000)(v, o); }},
        {0, "migrate", "h3", false, "", "Demo connection migration (new local port, path validation)",
            [this](const std::string& v, Options& o) { return SetFlag(o.tp.migrate, v, o); }},
        {0, "migrate-to", "h3", true, "<ip[:port]>", "Target local address for migration",
            [this](const std::string& v, Options& o) { o.tp.migrate_to = v; return true; }},
        {0, "migrate-delay", "h3", true, "<ms>", "Trigger migration mid-transfer after delay",
            [this](const std::string& v, Options& o) { return SetUint(o.tp.migrate_delay_ms, 0, 3600000)(v, o); }},
        {0, "push", "h3", false, "", "Accept and display HTTP/3 server push",
            [this](const std::string& v, Options& o) { return SetFlag(o.tp.push, v, o); }},
        {0, "stats", "h3", false, "", "Print QUIC/HTTP3 timing summary to stderr",
            [this](const std::string& v, Options& o) { return SetFlag(o.out.stats, v, o); }},

        // ---- output group ----
        {'o', "output", "output", true, "<file>", "Write body to file instead of stdout",
            [this](const std::string& v, Options& o) { o.out.output_files.push_back(v); return true; }},
        {'O', "remote-name", "output", false, "", "Write body to file named by URL",
            [this](const std::string& v, Options& o) { return SetFlag(o.out.remote_name, v, o); }},
        {'i', "include", "output", false, "", "Include response headers in output",
            [this](const std::string& v, Options& o) { return SetFlag(o.out.include_headers, v, o); }},
        {'w', "write-out", "output", true, "<fmt>", "Print result after transfer, e.g. '%{http_code}'",
            [this](const std::string& v, Options& o) { o.out.write_out = v; return true; }},
        {'f', "fail", "output", false, "", "Fail silently on HTTP >= 400 (exit 22, no body)",
            [this](const std::string& v, Options& o) { return SetFlag(o.out.fail, v, o); }},
        {'v', "verbose", "output", false, "", "Verbose diagnostics on stderr",
            [this](const std::string& v, Options& o) { return SetFlag(o.out.verbose, v, o); }},
        {'s', "silent", "output", false, "", "Silent mode (no progress/meter)",
            [this](const std::string& v, Options& o) { return SetFlag(o.out.silent, v, o); }},
        {'S', "show-error", "output", false, "", "Show errors even in silent mode",
            [this](const std::string& v, Options& o) { return SetFlag(o.out.show_error, v, o); }},
        {'#', "progress-bar", "output", false, "", "Progress bar instead of meter",
            [this](const std::string& v, Options& o) { return SetFlag(o.out.progress_bar, v, o); }},

        // ---- common group ----
        {'h', "help", "common", false, "", "Show this help and exit",
            [this](const std::string& v, Options& o) { return SetFlag(o.show_help, v, o); }},
    };
}

const OptionSpec* OptionTable::FindShort(char c) const {
    for (const auto& s : specs_)
        if (s.short_name == c) return &s;
    return nullptr;
}
const OptionSpec* OptionTable::FindLong(const std::string& name) const {
    for (const auto& s : specs_)
        if (s.long_name && name == s.long_name) return &s;
    return nullptr;
}

bool OptionTable::Parse(int argc, char* argv[], Options& opts, std::string& error) const {
    bool no_more_options = false;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (no_more_options || arg == "-" || arg.size() < 2 || arg[0] != '-') {
            opts.req.urls.push_back(arg);
            continue;
        }
        if (arg == "--") { no_more_options = true; continue; }

        const OptionSpec* spec = nullptr;
        std::string value;
        bool have_value = false;

        if (arg[1] == '-') {  // long: --name, --name=value, --name value
            std::string body = arg.substr(2);
            size_t eq = body.find('=');
            std::string name = (eq == std::string::npos) ? body : body.substr(0, eq);
            spec = FindLong(name);
            if (!spec) { error = "unknown option: --" + name; return false; }
            if (eq != std::string::npos) { value = body.substr(eq + 1); have_value = true; }
        } else {              // short: -abc, -ovalue, -o value
            const std::string cluster = arg.substr(1);
            for (size_t ci = 0; ci < cluster.size(); ++ci) {
                spec = FindShort(cluster[ci]);
                if (!spec) { error = std::string("unknown option: -") + cluster[ci]; return false; }
                if (spec->takes_value) {
                    if (ci + 1 < cluster.size()) { value = cluster.substr(ci + 1); have_value = true; }
                    break;
                }
                if (!spec->apply("", opts)) { error = std::string("bad flag: -") + cluster[ci]; return false; }
                spec = nullptr;  // consumed as flag, maybe more in cluster
            }
            if (!spec) continue;  // whole cluster was flags
        }

        if (spec->takes_value && !have_value) {
            if (i + 1 >= argc) { error = std::string("option requires value: ") + argv[i]; return false; }
            value = argv[++i];
        }
        if (!have_value && !spec->takes_value) value = "";
        if (!spec->apply(value, opts)) {
            error = std::string("invalid value for option: ") + argv[i] + " '" + value + "'";
            return false;
        }
    }
    return true;
}

void OptionTable::PrintGroup(const char* group, std::FILE* out) const {
    for (const auto& s : specs_) {
        if (strcmp(s.group, group) != 0) continue;
        char left[64];
        if (s.short_name && s.long_name)
            std::snprintf(left, sizeof(left), "  -%c, --%s %s", s.short_name, s.long_name, s.arg_name);
        else if (s.short_name)
            std::snprintf(left, sizeof(left), "  -%c %s", s.short_name, s.arg_name);
        else
            std::snprintf(left, sizeof(left), "      --%s %s", s.long_name, s.arg_name);
        std::fprintf(out, "%-42s %s\n", left, s.help);
    }
}

void OptionTable::PrintHelp(const char* program, std::FILE* out) const {
    std::fprintf(out, "Usage: %s [options] <URL> [URL...]\n", program);
    std::fprintf(out, "HTTP/3 command-line tool powered by quicx (curl-like + QUIC features)\n\n");
    std::fprintf(out, "HTTP:\n");            PrintGroup("http", out);
    std::fprintf(out, "\nTransport:\n");     PrintGroup("transport", out);
    std::fprintf(out, "\nHTTP/3 & QUIC specific:\n"); PrintGroup("h3", out);
    std::fprintf(out, "\nOutput:\n");        PrintGroup("output", out);
    std::fprintf(out, "\nCommon:\n");        PrintGroup("common", out);
    std::fprintf(out, "\nExample: %s -v --qlog ./qlogs https://host:5000/api/data\n", program);
}
