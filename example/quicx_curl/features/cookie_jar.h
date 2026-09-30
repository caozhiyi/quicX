#ifndef TOOL_QC_FEATURES_COOKIE_JAR_H
#define TOOL_QC_FEATURES_COOKIE_JAR_H

#include <string>
#include <utility>
#include <vector>

// Session-scoped cookie jar: -b load (Netscape file or "k=v; k2=v2" string),
// Set-Cookie absorption during transfers, -c save (Netscape format).
// Scope matching: host-suffix domain match + path-prefix, enough for
// example/test usage (no expiry, no secure-only enforcement).
class CookieJar {
public:
    // "-b file" when value contains a path separator or file exists; treated
    // as inline cookie string otherwise.
    void LoadFromSpec(const std::string& spec);

    void AbsorbResponse(const std::vector<std::pair<std::string, std::string>>& headers,
        const std::string& request_url);

    // "k1=v1; k2=v2" for cookies matching the URL, or empty.
    std::string CookieHeaderFor(const std::string& url) const;

    bool SaveTo(const std::string& file) const;

    bool empty() const { return cookies_.empty(); }

private:
    struct Cookie {
        std::string name;
        std::string value;
        std::string domain;  // stored without leading dot
        std::string path;
    };

    void SetCookie(const Cookie& c);
    static bool DomainMatches(const std::string& host, const std::string& domain);

    std::vector<Cookie> cookies_;
};

#endif  // TOOL_QC_FEATURES_COOKIE_JAR_H
