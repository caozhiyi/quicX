#include "features/cookie_jar.h"

#include <cstdio>
#include <fstream>
#include <sstream>

#include "app/url.h"

namespace {

std::string Trim(const std::string& s) {
    size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return "";
    size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

std::string Lower(const std::string& s) {
    std::string out = s;
    for (auto& c : out)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return out;
}

bool FileExists(const std::string& p) {
    std::FILE* f =
#ifdef _WIN32
        nullptr;
    if (fopen_s(&f, p.c_str(), "rb") != 0) f = nullptr;
#else
        std::fopen(p.c_str(), "rb");
#endif
    if (!f) return false;
    std::fclose(f);
    return true;
}

}  // namespace

void CookieJar::LoadFromSpec(const std::string& spec) {
    bool is_file = spec.find('/') != std::string::npos || spec.find('\\') != std::string::npos ||
                   spec.find('.') != std::string::npos;
    if (!is_file) {
        // inline "k1=v1; k2=v2" — session-wide (domain wildcard)
        std::istringstream ss(spec);
        std::string pair;
        while (std::getline(ss, pair, ';')) {
            size_t eq = pair.find('=');
            if (eq == std::string::npos) continue;
            Cookie c;
            c.name = Trim(pair.substr(0, eq));
            c.value = Trim(pair.substr(eq + 1));
            c.domain = "";
            c.path = "/";
            if (!c.name.empty()) SetCookie(c);
        }
        return;
    }
    if (!FileExists(spec)) return;
    std::ifstream in(spec);
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream ls(line);
        std::vector<std::string> f;
        std::string item;
        while (std::getline(ls, item, '\t') && f.size() < 7) f.push_back(item);
        if (f.size() < 7) continue;
        Cookie c;
        c.domain = f[0];
        if (!c.domain.empty() && c.domain[0] == '.') c.domain.erase(0, 1);
        c.path = f[2];
        c.name = f[5];
        c.value = f[6];
        if (!c.name.empty()) SetCookie(c);
    }
}

bool CookieJar::DomainMatches(const std::string& host, const std::string& domain) {
    if (domain.empty()) return true;  // session-wide inline cookie
    std::string h = Lower(host), d = Lower(domain);
    return h == d || (h.size() > d.size() && h.compare(h.size() - d.size(), d.size(), d) == 0 &&
                      h[h.size() - d.size() - 1] == '.');
}

void CookieJar::SetCookie(const Cookie& c) {
    for (auto& existing : cookies_) {
        if (existing.name == c.name && existing.domain == c.domain && existing.path == c.path) {
            existing.value = c.value;
            return;
        }
    }
    cookies_.push_back(c);
}

void CookieJar::AbsorbResponse(const std::vector<std::pair<std::string, std::string>>& headers,
    const std::string& request_url) {
    ParsedUrl url;
    bool have_url = ParseUrl(request_url, url);
    for (const auto& h : headers) {
        if (Lower(h.first) != "set-cookie") continue;
        Cookie c;
        c.path = "/";
        // split "k=v; attr; attr"
        std::istringstream ss(h.second);
        std::string part;
        bool first = true;
        while (std::getline(ss, part, ';')) {
            std::string t = Trim(part);
            if (first) {
                first = false;
                size_t eq = t.find('=');
                if (eq == std::string::npos) break;
                c.name = Trim(t.substr(0, eq));
                c.value = Trim(t.substr(eq + 1));
                continue;
            }
            std::string lt = Lower(t);
            if (lt.rfind("domain=", 0) == 0) {
                c.domain = t.substr(7);
                if (!c.domain.empty() && c.domain[0] == '.') c.domain.erase(0, 1);
            } else if (lt.rfind("path=", 0) == 0) {
                c.path = t.substr(5);
                if (c.path.empty() || c.path[0] != '/') c.path = "/";
            }
            // secure/httponly/max-age/expires: ignored for this tool
        }
        if (c.name.empty()) continue;
        if (c.domain.empty() && have_url) c.domain = url.host;
        SetCookie(c);
    }
}

std::string CookieJar::CookieHeaderFor(const std::string& request_url) const {
    ParsedUrl url;
    if (!ParseUrl(request_url, url)) return "";
    std::string out;
    for (const auto& c : cookies_) {
        if (!DomainMatches(url.host, c.domain)) continue;
        if (url.path.compare(0, c.path.size(), c.path) != 0) continue;
        if (!out.empty()) out += "; ";
        out += c.name + "=" + c.value;
    }
    return out;
}

bool CookieJar::SaveTo(const std::string& file) const {
    std::ofstream out(file, std::ios::trunc);
    if (!out.is_open()) return false;
    out << "# Netscape HTTP Cookie File\n";
    for (const auto& c : cookies_) {
        out << c.domain << "\tTRUE\t" << c.path << "\tFALSE\t0\t" << c.name << "\t" << c.value << "\n";
    }
    return true;
}
