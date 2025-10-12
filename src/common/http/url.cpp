#include <cctype>
#include <sstream>
#include <iomanip>

#include "common/http/url.h"

namespace quicx {
namespace common {

bool ParseURLForPseudoHeaders(const std::string& url_str, 
                              std::string& scheme,
                              std::string& host,
                              uint16_t& port,
                              std::string& path_with_query) {
    if (url_str.empty()) {
        return false;
    }
    
    size_t pos = 0;
    
    // Parse scheme
    size_t scheme_end = url_str.find("://");
    if (scheme_end == std::string::npos) {
        return false;
    }
    scheme = url_str.substr(0, scheme_end);
    pos = scheme_end + 3;
    
    // Parse host and port
    size_t host_end = url_str.find_first_of("/?#", pos);
    std::string host_port = url_str.substr(pos, host_end - pos);
    
    size_t port_pos = host_port.find(":");
    if (port_pos != std::string::npos) {
        host = host_port.substr(0, port_pos);
        try {
            port = static_cast<uint16_t>(std::stoi(host_port.substr(port_pos + 1)));
        } catch (...) {
            return false;
        }
    } else {
        host = host_port;
        // Set default ports
        if (scheme == "http") {
            port = 80;
        } else if (scheme == "https") {
            port = 443;
        } else {
            port = 0;
        }
    }
    
    // Extract path with query string (but without fragment)
    if (host_end != std::string::npos) {
        size_t fragment_pos = url_str.find('#', host_end);
        if (fragment_pos != std::string::npos) {
            // Exclude fragment (RFC 9114: fragments are NOT sent to server)
            path_with_query = url_str.substr(host_end, fragment_pos - host_end);
        } else {
            path_with_query = url_str.substr(host_end);
        }
    } else {
        path_with_query = "";
    }
    
    // Ensure path starts with /
    if (path_with_query.empty() || path_with_query[0] != '/') {
        path_with_query = "/" + path_with_query;
    }
    
    return true;
}

// URL encode (RFC 3986)
std::string URLEncode(const std::string& value) {
    std::ostringstream escaped;
    escaped.fill('0');
    escaped << std::hex;
    
    for (unsigned char c : value) {
        // Keep alphanumeric and unreserved characters: -_.~
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            escaped << c;
        } else {
            // Percent-encode everything else
            escaped << std::uppercase;
            escaped << '%' << std::setw(2) << static_cast<int>(c);
            escaped << std::nouppercase;
        }
    }
    
    return escaped.str();
}

// URL decode
std::string URLDecode(const std::string& value) {
    std::string result;
    result.reserve(value.length());
    
    for (size_t i = 0; i < value.length(); ++i) {
        if (value[i] == '%') {
            // Percent-encoded character
            if (i + 2 < value.length()) {
                int decoded_value;
                std::istringstream is(value.substr(i + 1, 2));
                if (is >> std::hex >> decoded_value) {
                    result += static_cast<char>(decoded_value);
                    i += 2;
                } else {
                    // Invalid encoding, keep as-is
                    result += value[i];
                }
            } else {
                // Incomplete encoding, keep as-is
                result += value[i];
            }
        } else if (value[i] == '+') {
            // Plus sign represents space in query strings
            result += ' ';
        } else {
            result += value[i];
        }
    }
    
    return result;
}

// Parse query string
bool ParseQueryString(const std::string& query_str, std::unordered_map<std::string, std::string>& params) {
    if (query_str.empty()) {
        return true;
    }
    
    size_t param_start = 0;
    size_t param_end;
    
    while ((param_end = query_str.find('&', param_start)) != std::string::npos) {
        std::string param = query_str.substr(param_start, param_end - param_start);
        size_t eq_pos = param.find('=');
        
        if (eq_pos != std::string::npos) {
            std::string key = URLDecode(param.substr(0, eq_pos));
            std::string value = URLDecode(param.substr(eq_pos + 1));
            params[key] = value;
        } else if (!param.empty()) {
            // Key without value (e.g., "?debug")
            params[URLDecode(param)] = "";
        }
        
        param_start = param_end + 1;
    }
    
    // Last parameter
    std::string param = query_str.substr(param_start);
    if (!param.empty()) {
        size_t eq_pos = param.find('=');
        if (eq_pos != std::string::npos) {
            std::string key = URLDecode(param.substr(0, eq_pos));
            std::string value = URLDecode(param.substr(eq_pos + 1));
            params[key] = value;
        } else {
            params[URLDecode(param)] = "";
        }
    }
    
    return true;
}

// Serialize query parameters
std::string SerializeQueryString(const std::unordered_map<std::string, std::string>& params) {
    if (params.empty()) {
        return "";
    }
    
    std::ostringstream ss;
    bool first = true;
    
    for (const auto& param : params) {
        if (!first) {
            ss << '&';
        }
        ss << URLEncode(param.first);
        if (!param.second.empty()) {
            ss << '=' << URLEncode(param.second);
        }
        first = false;
    }
    
    return ss.str();
}

// Build :authority pseudo-header
std::string BuildAuthority(const std::string& host, uint16_t port, const std::string& scheme) {
    // Omit default ports
    if ((scheme == "http" && port == 80) || 
        (scheme == "https" && port == 443) ||
        port == 0) {
        return host;
    }
    
    return host + ":" + std::to_string(port);
}

// Build :path pseudo-header
std::string BuildPathWithQuery(const std::string& path, const std::unordered_map<std::string, std::string>& query) {
    std::string result = path.empty() ? "/" : path;
    
    // Ensure path starts with /
    if (result[0] != '/') {
        result = "/" + result;
    }
    
    if (!query.empty()) {
        result += "?" + SerializeQueryString(query);
    }
    
    return result;
}

// Parse :path pseudo-header
bool ParsePathWithQuery(const std::string& path_with_query, std::string& path, 
                        std::unordered_map<std::string, std::string>& query) {
    if (path_with_query.empty()) {
        return false;
    }
    
    size_t query_pos = path_with_query.find('?');
    
    if (query_pos != std::string::npos) {
        path = path_with_query.substr(0, query_pos);
        std::string query_str = path_with_query.substr(query_pos + 1);
        return ParseQueryString(query_str, query);

    } else {
        path = path_with_query;
        query.clear();
        return true;
    }
}

}
}
