#ifndef COMMON_HTTP_URL
#define COMMON_HTTP_URL

#include <string>
#include <cstdint>
#include <unordered_map>

namespace quicx {
namespace common {

// Parse URL for HTTP/3 pseudo-headers (client-side, no query parsing needed)
// Extracts: scheme, host, port, path_with_query (includes ?query but excludes #fragment)
// This is more efficient for client-side as it doesn't parse query string into map
bool ParseURLForPseudoHeaders(const std::string& url_str, 
                              std::string& scheme,
                              std::string& host,
                              uint16_t& port,
                              std::string& path_with_query);

// URL encode/decode utilities (RFC 3986)
// Encode unsafe characters for use in URL components
std::string URLEncode(const std::string& value);

// Decode percent-encoded characters
std::string URLDecode(const std::string& value);

// Parse query string into key-value pairs
// Example: "key1=value1&key2=value2" -> {{"key1", "value1"}, {"key2", "value2"}}
bool ParseQueryString(const std::string& query_str, std::unordered_map<std::string, std::string>& params);

// Serialize query parameters into query string
// Example: {{"key1", "value1"}, {"key2", "value2"}} -> "key1=value1&key2=value2"
std::string SerializeQueryString(const std::unordered_map<std::string, std::string>& params);

// Build :authority pseudo-header from host and port
// Example: ("example.com", 8080, "https") -> "example.com:8080"
//          ("example.com", 443, "https") -> "example.com"
std::string BuildAuthority(const std::string& host, uint16_t port, const std::string& scheme);

// Build :path pseudo-header from path and query parameters
// Example: ("/api/users", {{"page", "1"}}) -> "/api/users?page=1"
std::string BuildPathWithQuery(const std::string& path, const std::unordered_map<std::string, std::string>& query);

// Parse :path pseudo-header into path and query parameters
// Example: "/api/users?page=1&limit=10" -> ("/api/users", {{"page", "1"}, {"limit", "10"}})
bool ParsePathWithQuery(const std::string& path_with_query, std::string& path, 
                        std::unordered_map<std::string, std::string>& query);

}
}

#endif
