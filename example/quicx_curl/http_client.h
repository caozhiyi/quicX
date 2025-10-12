#ifndef TOOL_QUICX_CURL_HTTP_CLIENT
#define TOOL_QUICX_CURL_HTTP_CLIENT

#include <string>
#include <memory>
#include <mutex>
#include <condition_variable>
#include "http3/include/if_client.h"

struct HttpResponse {
    uint32_t status_code = 0;
    std::unordered_map<std::string, std::string> headers;
    std::string body;
    uint32_t error = 0;
    bool completed = false;
    
    // Timing information
    uint64_t start_time_ms = 0;
    uint64_t end_time_ms = 0;
    
    uint64_t GetDurationMs() const {
        return end_time_ms - start_time_ms;
    }
};

class HttpClient {
public:
    HttpClient();
    ~HttpClient();
    
    // Initialize client
    bool Init(bool verbose = false);
    
    // Perform synchronous HTTP request
    bool DoRequest(const std::string& url,
                  const std::string& method,
                  const std::vector<std::string>& headers,
                  const std::string& data,
                  HttpResponse& response);
    
private:
    std::unique_ptr<quicx::http3::IClient> client_;
    std::mutex mutex_;
    std::condition_variable cv_;
    bool verbose_;
    
    static quicx::http3::HttpMethod StringToMethod(const std::string& method);
};


#endif

