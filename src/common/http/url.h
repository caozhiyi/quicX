#ifndef COMMON_HTTP_URL
#define COMMON_HTTP_URL

#include <string>
#include <unordered_map>

namespace quicx {
namespace common {

// URL structure containing all components
struct URL {
    std::string scheme;
    std::string host;
    uint16_t port;
    std::string path;
    std::unordered_map<std::string, std::string> query;
    std::string fragment;

    URL() : port(0) {}
};

// Parse URL string into URL structure
// Returns true if parsing successful, false otherwise
bool ParseURL(const std::string& url_str, URL& url);

// Serialize URL structure back to string
std::string SerializeURL(const URL& url);

}
}

#endif
