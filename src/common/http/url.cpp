#include "common/http/url.h"
#include <sstream>

namespace quicx {
namespace common {

bool ParseURL(const std::string& url_str, URL& url) {
    if (url_str.empty()) {
        return false;
    }

    size_t pos = 0;
    
    // Parse scheme
    size_t scheme_end = url_str.find("://");
    if (scheme_end != std::string::npos) {
        url.scheme = url_str.substr(0, scheme_end);
        pos = scheme_end + 3;
    } else {
        return false;
    }

    // Parse host and port
    size_t host_end = url_str.find_first_of("/?#", pos);
    std::string host_port = url_str.substr(pos, host_end - pos);
    size_t port_pos = host_port.find(":");
    
    if (port_pos != std::string::npos) {
        url.host = host_port.substr(0, port_pos);
        try {
            url.port = static_cast<uint16_t>(std::stoi(host_port.substr(port_pos + 1)));
        } catch (...) {
            return false;
        }

    } else {
        url.host = host_port;
        // Default ports
        if (url.scheme == "http") {
            url.port = 80;

        } else if (url.scheme == "https") {
            url.port = 443;
        }
    }

    if (host_end == std::string::npos) {
        return true;
    }
    pos = host_end;

    // Parse path
    size_t path_end = url_str.find_first_of("?#", pos);
    if (pos < url_str.length()) {
        url.path = url_str.substr(pos, path_end - pos);
    }

    if (path_end == std::string::npos) {
        return true;
    }
    pos = path_end;

    // Parse query parameters
    if (url_str[pos] == '?') {
        pos++;
        size_t query_end = url_str.find('#', pos);
        std::string query_str = url_str.substr(pos, query_end - pos);
        
        size_t param_start = 0;
        size_t param_end;
        while ((param_end = query_str.find('&', param_start)) != std::string::npos) {
            std::string param = query_str.substr(param_start, param_end - param_start);
            size_t eq_pos = param.find('=');
            if (eq_pos != std::string::npos) {
                url.query[param.substr(0, eq_pos)] = param.substr(eq_pos + 1);
            }
            param_start = param_end + 1;
        }
        std::string param = query_str.substr(param_start);
        size_t eq_pos = param.find('=');
        if (eq_pos != std::string::npos) {
            url.query[param.substr(0, eq_pos)] = param.substr(eq_pos + 1);
        }
        
        pos = query_end;
    }

    // Parse fragment
    if (pos != std::string::npos && pos < url_str.length() && url_str[pos] == '#') {
        url.fragment = url_str.substr(pos + 1);
    }

    return true;
}

std::string SerializeURL(const URL& url) {
    std::stringstream ss;
    
    if (!url.scheme.empty()) {
        ss << url.scheme << "://";
    }
    
    ss << url.host;
    
    if (url.port != 0 && 
        !((url.scheme == "http" && url.port == 80) || 
          (url.scheme == "https" && url.port == 443))) {
        ss << ":" << url.port;
    }
    
    if (!url.path.empty()) {
        if (url.path[0] != '/') {
            ss << '/';
        }
        ss << url.path;
    }
    
    if (!url.query.empty()) {
        ss << '?';
        bool first = true;
        for (const auto& param : url.query) {
            if (!first) {
                ss << '&';
            }
            ss << param.first << '=' << param.second;
            first = false;
        }
    }
    
    if (!url.fragment.empty()) {
        ss << '#' << url.fragment;
    }
    
    return ss.str();
}

}
}
