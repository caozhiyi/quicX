#include <iostream>
#include <string>
#include <sstream>
#include <thread>
#include <chrono>
#include <atomic>
#include <mutex>
#include <ctime>
#include <random>
#include <iomanip>
#include "http3/include/if_server.h"
#include "http3/include/if_request.h"
#include "http3/include/if_response.h"

// Request statistics
struct RequestStats {
    std::atomic<uint64_t> total_requests{0};
    std::atomic<uint64_t> fast_requests{0};
    std::atomic<uint64_t> medium_requests{0};
    std::atomic<uint64_t> slow_requests{0};
    std::atomic<uint64_t> concurrent_requests{0};
    std::atomic<uint64_t> max_concurrent{0};
    std::mutex mutex;
    std::chrono::steady_clock::time_point start_time;

    RequestStats() {
        start_time = std::chrono::steady_clock::now();
    }

    void IncrementConcurrent() {
        uint64_t current = ++concurrent_requests;
        uint64_t max = max_concurrent.load();
        while (current > max && !max_concurrent.compare_exchange_weak(max, current)) {
            max = max_concurrent.load();
        }
    }

    void DecrementConcurrent() {
        --concurrent_requests;
    }

    std::string GetStatsJson() {
        auto now = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::seconds>(now - start_time);
        
        std::ostringstream oss;
        oss << "{"
            << "\"total_requests\":" << total_requests.load()
            << ",\"fast_requests\":" << fast_requests.load()
            << ",\"medium_requests\":" << medium_requests.load()
            << ",\"slow_requests\":" << slow_requests.load()
            << ",\"current_concurrent\":" << concurrent_requests.load()
            << ",\"max_concurrent\":" << max_concurrent.load()
            << ",\"uptime_seconds\":" << duration.count()
            << ",\"requests_per_second\":" << (duration.count() > 0 ? total_requests.load() / duration.count() : 0)
            << "}";
        return oss.str();
    }
};

class RAII_ConcurrentCounter {
    RequestStats& stats_;
public:
    RAII_ConcurrentCounter(RequestStats& stats) : stats_(stats) {
        stats_.IncrementConcurrent();
    }
    ~RAII_ConcurrentCounter() {
        stats_.DecrementConcurrent();
    }
};

std::string GetCurrentTime() {
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;
    
    char buf[100];
    std::strftime(buf, sizeof(buf), "%H:%M:%S", std::localtime(&time_t_now));
    
    std::ostringstream oss;
    oss << buf << "." << std::setfill('0') << std::setw(3) << ms.count();
    return oss.str();
}

int main() {
    static const char cert_pem[] =
      "-----BEGIN CERTIFICATE-----\n"
      "MIICWDCCAcGgAwIBAgIJAPuwTC6rEJsMMA0GCSqGSIb3DQEBBQUAMEUxCzAJBgNV\n"
      "BAYTAkFVMRMwEQYDVQQIDApTb21lLVN0YXRlMSEwHwYDVQQKDBhJbnRlcm5ldCBX\n"
      "aWRnaXRzIFB0eSBMdGQwHhcNMTQwNDIzMjA1MDQwWhcNMTcwNDIyMjA1MDQwWjBF\n"
      "MQswCQYDVQQGEwJBVTETMBEGA1UECAwKU29tZS1TdGF0ZTEhMB8GA1UECgwYSW50\n"
      "ZXJuZXQgV2lkZ2l0cyBQdHkgTHRkMIGfMA0GCSqGSIb3DQEBAQUAA4GNADCBiQKB\n"
      "gQDYK8imMuRi/03z0K1Zi0WnvfFHvwlYeyK9Na6XJYaUoIDAtB92kWdGMdAQhLci\n"
      "HnAjkXLI6W15OoV3gA/ElRZ1xUpxTMhjP6PyY5wqT5r6y8FxbiiFKKAnHmUcrgfV\n"
      "W28tQ+0rkLGMryRtrukXOgXBv7gcrmU7G1jC2a7WqmeI8QIDAQABo1AwTjAdBgNV\n"
      "HQ4EFgQUi3XVrMsIvg4fZbf6Vr5sp3Xaha8wHwYDVR0jBBgwFoAUi3XVrMsIvg4f\n"
      "Zbf6Vr5sp3Xaha8wDAYDVR0TBAUwAwEB/zANBgkqhkiG9w0BAQUFAAOBgQA76Hht\n"
      "ldY9avcTGSwbwoiuIqv0jTL1fHFnzy3RHMLDh+Lpvolc5DSrSJHCP5WuK0eeJXhr\n"
      "T5oQpHL9z/cCDLAKCKRa4uV0fhEdOWBqyR9p8y5jJtye72t6CuFUV5iqcpF4BH4f\n"
      "j2VNHwsSrJwkD4QUGlUtH7vwnQmyCFxZMmWAJg==\n"
      "-----END CERTIFICATE-----\n";

    static const char key_pem[] =
      "-----BEGIN RSA PRIVATE KEY-----\n"
      "MIICXgIBAAKBgQDYK8imMuRi/03z0K1Zi0WnvfFHvwlYeyK9Na6XJYaUoIDAtB92\n"
      "kWdGMdAQhLciHnAjkXLI6W15OoV3gA/ElRZ1xUpxTMhjP6PyY5wqT5r6y8FxbiiF\n"
      "KKAnHmUcrgfVW28tQ+0rkLGMryRtrukXOgXBv7gcrmU7G1jC2a7WqmeI8QIDAQAB\n"
      "AoGBAIBy09Fd4DOq/Ijp8HeKuCMKTHqTW1xGHshLQ6jwVV2vWZIn9aIgmDsvkjCe\n"
      "i6ssZvnbjVcwzSoByhjN8ZCf/i15HECWDFFh6gt0P5z0MnChwzZmvatV/FXCT0j+\n"
      "WmGNB/gkehKjGXLLcjTb6dRYVJSCZhVuOLLcbWIV10gggJQBAkEA8S8sGe4ezyyZ\n"
      "m4e9r95g6s43kPqtj5rewTsUxt+2n4eVodD+ZUlCULWVNAFLkYRTBCASlSrm9Xhj\n"
      "QpmWAHJUkQJBAOVzQdFUaewLtdOJoPCtpYoY1zd22eae8TQEmpGOR11L6kbxLQsk\n"
      "aMly/DOnOaa82tqAGTdqDEZgSNmCeKKknmECQAvpnY8GUOVAubGR6c+W90iBuQLj\n"
      "LtFp/9ihd2w/PoDwrHZaoUYVcT4VSfJQog/k7kjE4MYXYWL8eEKg3WTWQNECQQDk\n"
      "104Wi91Umd1PzF0ijd2jXOERJU1wEKe6XLkYYNHWQAe5l4J4MWj9OdxFXAxIuuR/\n"
      "tfDwbqkta4xcux67//khAkEAvvRXLHTaa6VFzTaiiO8SaFsHV3lQyXOtMrBpB5jd\n"
      "moZWgjHvB2W9Ckn7sDqsPB+U2tyX0joDdQEyuiMECDY8oQ==\n"
      "-----END RSA PRIVATE KEY-----\n";

    auto stats = std::make_shared<RequestStats>();
    auto server = quicx::IServer::Create();

    // Request logging middleware
    server->AddMiddleware(
        quicx::HttpMethod::kAny,
        quicx::MiddlewarePosition::kBefore,
        [](std::shared_ptr<quicx::IRequest> req, std::shared_ptr<quicx::IResponse> resp) {
            std::cout << "[" << GetCurrentTime() << "] [" 
                      << req->GetMethodString() << "] " << req->GetPath() << std::endl;
        }
    );

    // GET / - Welcome page
    server->AddHandler(
        quicx::HttpMethod::kGet,
        "/",
        [stats](std::shared_ptr<quicx::IRequest> req, std::shared_ptr<quicx::IResponse> resp) {
            std::string html = 
                "<!DOCTYPE html><html><head><title>Concurrent Requests Server</title></head>"
                "<body><h1>QuicX HTTP/3 Concurrent Requests Demo</h1>"
                "<p>This server demonstrates HTTP/3 multiplexing capabilities.</p>"
                "<h2>Endpoints:</h2><ul>"
                "<li><b>GET /fast</b> - Fast response (10ms delay)</li>"
                "<li><b>GET /medium</b> - Medium response (100ms delay)</li>"
                "<li><b>GET /slow</b> - Slow response (500ms delay)</li>"
                "<li><b>GET /random</b> - Random delay (10-500ms)</li>"
                "<li><b>GET /data/:size</b> - Generate data of specific size (KB)</li>"
                "<li><b>GET /stats</b> - Server statistics</li>"
                "</ul>"
                "<h2>Current Stats:</h2>"
                "<pre>" + stats->GetStatsJson() + "</pre>"
                "</body></html>";
            
            resp->AddHeader("Content-Type", "text/html");
            resp->AppendBody(html);
            resp->SetStatusCode(200);
        }
    );

    // GET /fast - Fast response (10ms)
    server->AddHandler(
        quicx::HttpMethod::kGet,
        "/fast",
        [stats](std::shared_ptr<quicx::IRequest> req, std::shared_ptr<quicx::IResponse> resp) {
            RAII_ConcurrentCounter counter(*stats);
            stats->total_requests++;
            stats->fast_requests++;
            
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            
            std::ostringstream oss;
            oss << "{\"endpoint\":\"fast\",\"delay_ms\":10,\"message\":\"Fast response\",\"timestamp\":\""
                << GetCurrentTime() << "\"}";
            
            resp->AddHeader("Content-Type", "application/json");
            resp->AppendBody(oss.str());
            resp->SetStatusCode(200);
        }
    );

    // GET /medium - Medium response (100ms)
    server->AddHandler(
        quicx::HttpMethod::kGet,
        "/medium",
        [stats](std::shared_ptr<quicx::IRequest> req, std::shared_ptr<quicx::IResponse> resp) {
            RAII_ConcurrentCounter counter(*stats);
            stats->total_requests++;
            stats->medium_requests++;
            
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            
            std::ostringstream oss;
            oss << "{\"endpoint\":\"medium\",\"delay_ms\":100,\"message\":\"Medium response\",\"timestamp\":\""
                << GetCurrentTime() << "\"}";
            
            resp->AddHeader("Content-Type", "application/json");
            resp->AppendBody(oss.str());
            resp->SetStatusCode(200);
        }
    );

    // GET /slow - Slow response (500ms)
    server->AddHandler(
        quicx::HttpMethod::kGet,
        "/slow",
        [stats](std::shared_ptr<quicx::IRequest> req, std::shared_ptr<quicx::IResponse> resp) {
            RAII_ConcurrentCounter counter(*stats);
            stats->total_requests++;
            stats->slow_requests++;
            
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            
            std::ostringstream oss;
            oss << "{\"endpoint\":\"slow\",\"delay_ms\":500,\"message\":\"Slow response\",\"timestamp\":\""
                << GetCurrentTime() << "\"}";
            
            resp->AddHeader("Content-Type", "application/json");
            resp->AppendBody(oss.str());
            resp->SetStatusCode(200);
        }
    );

    // GET /random - Random delay (10-500ms)
    server->AddHandler(
        quicx::HttpMethod::kGet,
        "/random",
        [stats](std::shared_ptr<quicx::IRequest> req, std::shared_ptr<quicx::IResponse> resp) {
            RAII_ConcurrentCounter counter(*stats);
            stats->total_requests++;
            
            static std::random_device rd;
            static std::mt19937 gen(rd());
            static std::uniform_int_distribution<> dis(10, 500);
            
            int delay_ms = dis(gen);
            std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
            
            std::ostringstream oss;
            oss << "{\"endpoint\":\"random\",\"delay_ms\":" << delay_ms 
                << ",\"message\":\"Random delay response\",\"timestamp\":\""
                << GetCurrentTime() << "\"}";
            
            resp->AddHeader("Content-Type", "application/json");
            resp->AppendBody(oss.str());
            resp->SetStatusCode(200);
        }
    );

    // GET /data/:size - Generate data of specific size (in KB)
    server->AddHandler(
        quicx::HttpMethod::kGet,
        "/data/:size",
        [stats](std::shared_ptr<quicx::IRequest> req, std::shared_ptr<quicx::IResponse> resp) {
            RAII_ConcurrentCounter counter(*stats);
            stats->total_requests++;
            
            std::string path = req->GetPath();
            size_t last_slash = path.find_last_of('/');
            if (last_slash == std::string::npos) {
                resp->SetStatusCode(400);
                resp->AppendBody("{\"error\":\"Invalid path\"}");
                return;
            }
            
            std::string size_str = path.substr(last_slash + 1);
            int size_kb = 1;
            try {
                size_kb = std::stoi(size_str);
                if (size_kb < 1) size_kb = 1;
                if (size_kb > 1024) size_kb = 1024; // Max 1MB
            } catch (...) {
                resp->SetStatusCode(400);
                resp->AppendBody("{\"error\":\"Invalid size\"}");
                return;
            }
            
            // Generate data
            std::string data;
            data.reserve(size_kb * 1024);
            std::string pattern = "QuicX HTTP/3 Concurrent Test Data - ";
            size_t bytes_written = 0;
            size_t target_bytes = size_kb * 1024;
            
            while (bytes_written < target_bytes) {
                size_t to_write = std::min(pattern.size(), target_bytes - bytes_written);
                data.append(pattern, 0, to_write);
                bytes_written += to_write;
            }
            
            std::ostringstream header;
            header << "{\"size_kb\":" << size_kb << ",\"size_bytes\":" << data.size() 
                   << ",\"timestamp\":\"" << GetCurrentTime() << "\"}\n\n";
            
            resp->AddHeader("Content-Type", "text/plain");
            resp->AppendBody(header.str() + data);
            resp->SetStatusCode(200);
        }
    );

    // GET /stats - Server statistics
    server->AddHandler(
        quicx::HttpMethod::kGet,
        "/stats",
        [stats](std::shared_ptr<quicx::IRequest> req, std::shared_ptr<quicx::IResponse> resp) {
            resp->AddHeader("Content-Type", "application/json");
            resp->AppendBody(stats->GetStatsJson());
            resp->SetStatusCode(200);
        }
    );

    // Response middleware
    server->AddMiddleware(
        quicx::HttpMethod::kAny,
        quicx::MiddlewarePosition::kAfter,
        [](std::shared_ptr<quicx::IRequest> req, std::shared_ptr<quicx::IResponse> resp) {
            resp->AddHeader("X-Powered-By", "QuicX-HTTP3");
            resp->AddHeader("Access-Control-Allow-Origin", "*");
        }
    );

    // Configure and start server
    quicx::Http3ServerConfig config;
    config.cert_pem_ = cert_pem;
    config.key_pem_ = key_pem;
    config.config_.thread_num_ = 4; // More threads for concurrent handling
    config.config_.log_level_ = quicx::LogLevel::kError;
    
    server->Init(config);
    
    std::cout << "==================================" << std::endl;
    std::cout << "Concurrent Requests Server" << std::endl;
    std::cout << "==================================" << std::endl;
    std::cout << "Listen on: https://0.0.0.0:8885" << std::endl;
    std::cout << "Worker threads: 4" << std::endl;
    std::cout << std::endl;
    std::cout << "Endpoints:" << std::endl;
    std::cout << "  GET /fast        - 10ms delay" << std::endl;
    std::cout << "  GET /medium      - 100ms delay" << std::endl;
    std::cout << "  GET /slow        - 500ms delay" << std::endl;
    std::cout << "  GET /random      - Random delay (10-500ms)" << std::endl;
    std::cout << "  GET /data/:size  - Generate data (size in KB)" << std::endl;
    std::cout << "  GET /stats       - Statistics" << std::endl;
    std::cout << "==================================" << std::endl;
    std::cout << std::endl;

    if (!server->Start("0.0.0.0", 8885)) {
        std::cout << "Failed to start server" << std::endl;
        return 1;
    }
    
    server->Join();
    return 0;
}

