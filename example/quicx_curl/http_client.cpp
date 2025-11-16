#include <chrono>
#include <iostream>

#include "http_client.h"
#include "common/util/time.h"
#include "http3/include/if_client.h"
#include "http3/include/if_response.h"

HttpClient::HttpClient() : verbose_(false) {
}

HttpClient::~HttpClient() {
}

bool HttpClient::Init(bool verbose) {
    verbose_ = verbose;
    
    client_ = quicx::IClient::Create();
    if (!client_) {
        std::cerr << "Failed to create HTTP/3 client" << std::endl;
        return false;
    }
    
    quicx::Http3Config config;
    config.thread_num_ = 2;
    config.log_level_ = verbose ? quicx::LogLevel::kInfo : quicx::LogLevel::kError;
    
    if (!client_->Init(config)) {
        std::cerr << "Failed to initialize HTTP/3 client" << std::endl;
        return false;
    }
    
    if (verbose_) {
        std::cerr << "* HTTP/3 client initialized" << std::endl;
    }
    
    return true;
}

bool HttpClient::DoRequest(const std::string& url,
                          const std::string& method,
                          const std::vector<std::string>& headers,
                          const std::string& data,
                          HttpResponse& response) {
    if (!client_) {
        std::cerr << "Client not initialized" << std::endl;
        return false;
    }
    
    // Record start time
    response.start_time_ms = quicx::common::UTCTimeMsec();
    
    if (verbose_) {
        std::cerr << "* Preparing request to " << url << std::endl;
        std::cerr << "* Method: " << method << std::endl;
    }
    
    // Create request
    auto request = quicx::IRequest::Create();
    
    // Set body if provided
    if (!data.empty()) {
        request->AppendBody(data);
        if (verbose_) {
            std::cerr << "* Request body: " << data.length() << " bytes" << std::endl;
        }
    }
    
    // Add custom headers
    for (const auto& header : headers) {
        size_t colon_pos = header.find(':');
        if (colon_pos != std::string::npos) {
            std::string name = header.substr(0, colon_pos);
            std::string value = header.substr(colon_pos + 1);
            // Trim leading space from value
            while (!value.empty() && value[0] == ' ') {
                value = value.substr(1);
            }
            request->AddHeader(name, value);
            if (verbose_) {
                std::cerr << "* Header: " << name << ": " << value << std::endl;
            }
        }
    }
    
    // Add content-type for POST/PUT with data
    if (!data.empty() && (method == "POST" || method == "PUT")) {
        std::string content_type;
        if (!request->GetHeader("content-type", content_type) && 
            !request->GetHeader("Content-Type", content_type)) {
            request->AddHeader("content-type", "application/json");
        }
    }
    
    if (verbose_) {
        std::cerr << "* Sending request..." << std::endl;
    }
    
    // Send request with callback
    bool request_completed = false;
    {
        std::unique_lock<std::mutex> lock(mutex_);
        
        client_->DoRequest(
            url,
            StringToMethod(method),
            request,
            [this, &response, &request_completed](
                std::shared_ptr<quicx::IResponse> resp, uint32_t error) {
                std::unique_lock<std::mutex> lock(mutex_);
                
                response.end_time_ms = quicx::common::UTCTimeMsec();
                response.error = error;
                
                if (error == 0 && resp) {
                    response.status_code = resp->GetStatusCode();
                    response.headers = resp->GetHeaders();
                    //response.body = resp->GetBodyAsString(); TODO
                }
                
                response.completed = true;
                request_completed = true;
                cv_.notify_one();
            }
        );
        
        // Wait for response (with timeout)
        auto timeout = std::chrono::seconds(30);
        if (!cv_.wait_for(lock, timeout, [&request_completed]{ return request_completed; })) {
            std::cerr << "Request timeout" << std::endl;
            return false;
        }
    }
    
    if (verbose_) {
        std::cerr << "* Request completed in " << response.GetDurationMs() << " ms" << std::endl;
        std::cerr << "* Status: " << response.status_code << std::endl;
    }
    
    return response.error == 0;
}

quicx::HttpMethod HttpClient::StringToMethod(const std::string& method) {
    if (method == "GET") return quicx::HttpMethod::kGet;
    if (method == "POST") return quicx::HttpMethod::kPost;
    if (method == "PUT") return quicx::HttpMethod::kPut;
    if (method == "DELETE") return quicx::HttpMethod::kDelete;
    if (method == "HEAD") return quicx::HttpMethod::kHead;
    if (method == "OPTIONS") return quicx::HttpMethod::kOptions;
    if (method == "PATCH") return quicx::HttpMethod::kPatch;
    if (method == "TRACE") return quicx::HttpMethod::kTrace;
    if (method == "CONNECT") return quicx::HttpMethod::kConnect;
    
    return quicx::HttpMethod::kGet;  // Default
}
