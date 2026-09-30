#ifndef TOOL_QC_APP_URL_H
#define TOOL_QC_APP_URL_H

#include <string>

// Minimal URL utilities (header-only): parsing and RFC 3986-style relative
// reference resolution for Location headers. Accepts both
// "https://host:port/path" and bare "host:port/path" (scheme defaults to
// https, as everything this tool speaks is HTTP/3).

struct ParsedUrl {
    std::string scheme;  // "https"
    std::string host;    // without port
    std::string port;    // "" if absent
    std::string path;    // "/" if absent (no query tracking needed by tool)

    std::string HostPort() const { return port.empty() ? host : host + ":" + port; }
    std::string Authority() const { return HostPort(); }
    std::string Str() const {
        return scheme + "://" + HostPort() + path;
    }
};

inline bool ParseUrl(const std::string& url, ParsedUrl& out) {
    std::string rest = url;
    size_t sep = rest.find("://");
    if (sep != std::string::npos) {
        out.scheme = rest.substr(0, sep);
        rest = rest.substr(sep + 3);
    } else {
        out.scheme = "https";
    }
    size_t slash = rest.find('/');
    std::string hostport = (slash == std::string::npos) ? rest : rest.substr(0, slash);
    out.path = (slash == std::string::npos) ? "/" : rest.substr(slash);
    if (hostport.empty()) return false;

    // IPv6 [::1]:443
    if (!hostport.empty() && hostport[0] == '[') {
        size_t rb = hostport.find(']');
        if (rb == std::string::npos) return false;
        out.host = hostport.substr(0, rb + 1);
        if (rb + 2 <= hostport.size() && hostport[rb + 1] == ':')
            out.port = hostport.substr(rb + 2);
        return true;
    }
    size_t colon = hostport.rfind(':');
    if (colon != std::string::npos) {
        out.host = hostport.substr(0, colon);
        out.port = hostport.substr(colon + 1);
    } else {
        out.host = hostport;
        out.port = "";
    }
    return !out.host.empty();
}

// Resolves `ref` (possibly relative) against `base`. Handles "", "/abs",
// "rel", "../rel", "//host/path" and absolute forms.
inline std::string JoinUrl(const std::string& base, const std::string& ref) {
    if (ref.empty()) return base;
    size_t sep = ref.find("://");
    if (sep != std::string::npos) return ref;              // absolute
    ParsedUrl b;
    if (!ParseUrl(base, b)) return ref;

    if (ref.size() >= 2 && ref[0] == '/' && ref[1] == '/')  // scheme-relative
        return b.scheme + ":" + ref;
    if (!ref.empty() && ref[0] == '/')                       // path-absolute
        return b.scheme + "://" + b.HostPort() + ref;

    // path-relative: merge with base directory
    std::string dir = b.path.substr(0, b.path.find_last_of('/') + 1);  // "/" if none
    std::string merged = dir + ref;
    // collapse "./" and "../"
    std::string out;
    size_t i = 0;
    while (i < merged.size()) {
        if (merged.compare(i, 2, "./") == 0) { i += 2; continue; }
        if (merged.compare(i, 3, "../") == 0 && !out.empty()) {
            size_t sl = out.find_last_of('/');
            if (sl != std::string::npos && sl > 0) out = out.substr(0, sl);
            else if (sl == 0) out = "/";
            i += 3;
            continue;
        }
        out += merged[i++];
    }
    if (out.empty() || out[0] != '/') out = "/" + out;
    return b.scheme + "://" + b.HostPort() + out;
}

#endif  // TOOL_QC_APP_URL_H
