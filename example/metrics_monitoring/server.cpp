#include <chrono>
#include <iostream>
#include <sstream>
#include <thread>
#include "common/metrics/metrics.h"
#include "common/metrics/metrics_std.h"
#include "http3/include/if_request.h"
#include "http3/include/if_response.h"
#include "http3/include/if_server.h"

// Custom application metrics
namespace {
quicx::common::MetricID custom_requests_total;
quicx::common::MetricID custom_request_duration_ms;
quicx::common::MetricID custom_active_requests;
quicx::common::MetricID custom_error_count;
}  // namespace

// RAII helper for tracking active requests
class RequestTracker {
public:
    RequestTracker() {
        quicx::common::Metrics::GaugeInc(custom_active_requests);
        start_time_ = std::chrono::steady_clock::now();
    }

    ~RequestTracker() {
        quicx::common::Metrics::GaugeDec(custom_active_requests);
        auto duration =
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start_time_)
                .count();
        quicx::common::Metrics::HistogramObserve(custom_request_duration_ms, duration);
    }

private:
    std::chrono::steady_clock::time_point start_time_;
};

// Print metrics summary to console
void PrintMetricsSummary() {
    std::cout << "\n" << std::string(80, '=') << std::endl;
    std::cout << " Metrics Summary (Real-time)" << std::endl;
    std::cout << std::string(80, '=') << std::endl;

    // Note: In production, you would query the metrics registry
    // For this demo, we'll show the Prometheus export
    std::string prometheus_output = quicx::common::Metrics::ExportPrometheus();

    // Parse and display key metrics
    // Parse and display key metrics
    std::cout << "\n Key Metrics:\n" << std::endl;

    // This is a simplified display - in production you'd parse the output
    std::istringstream iss(prometheus_output);
    std::string line;
    bool show_line = false;

    while (std::getline(iss, line)) {
        // Show custom metrics and important QUIC metrics
        if (line.find("custom_") != std::string::npos || line.find("quicx_quic_connections") != std::string::npos ||
            line.find("quicx_quic_packets") != std::string::npos ||
            line.find("quicx_http3_requests") != std::string::npos || line.find("quicx_rtt") != std::string::npos) {
            if (line.find("# HELP") != std::string::npos) {
                show_line = true;
                std::cout << "\n";
            }

            if (show_line) {
                std::cout << "  " << line << std::endl;
            }

            if (line.find("# TYPE") == std::string::npos && line.find("# HELP") == std::string::npos && !line.empty()) {
                show_line = false;
            }
        }
    }

    std::cout << "\n" << std::string(80, '=') << std::endl;
}

int main() {
    std::cout << " quicX Metrics Monitoring Example" << std::endl;
    std::cout << "===================================\n" << std::endl;

    // Step 1: Initialize Metrics System
    std::cout << " Step 1: Initializing metrics system..." << std::endl;
    quicx::MetricsConfig metrics_config;
    metrics_config.enable = true;
    metrics_config.prefix = "quicx";
    quicx::common::Metrics::Initialize(metrics_config);
    std::cout << "   Metrics system initialized. UdpPacketsRx ID: " << quicx::common::MetricsStd::UdpPacketsRx
              << std::endl;

    // Step 2: Register Custom Metrics
    std::cout << " Step 2: Registering custom application metrics..." << std::endl;

    custom_requests_total =
        quicx::common::Metrics::RegisterCounter("custom_requests_total", "Total number of HTTP requests processed");

    custom_active_requests =
        quicx::common::Metrics::RegisterGauge("custom_active_requests", "Number of currently active requests");

    custom_request_duration_ms = quicx::common::Metrics::RegisterHistogram("custom_request_duration_ms",
        "Request processing duration in milliseconds", {10, 25, 50, 100, 250, 500, 1000, 2500, 5000}  // Buckets
    );

    custom_error_count = quicx::common::Metrics::RegisterCounter("custom_error_count", "Total number of errors");

    std::cout << "   Registered 4 custom metrics\n" << std::endl;

    // Step 3: Create HTTP/3 Server
    std::cout << " Step 3: Creating HTTP/3 server..." << std::endl;

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

    auto server = quicx::IServer::Create();

    // Handler 1: Simple hello endpoint
    server->AddHandler(quicx::HttpMethod::kGet, "/hello",
        [](std::shared_ptr<quicx::IRequest> req, std::shared_ptr<quicx::IResponse> resp) {
            RequestTracker tracker;  // RAII tracking
            quicx::common::Metrics::CounterInc(custom_requests_total);

            std::cout << " Request: GET /hello" << std::endl;

            resp->AppendBody("Hello from quicX with metrics!");
            resp->SetStatusCode(200);
        });

    // Handler 2: Slow endpoint (simulates processing time)
    server->AddHandler(quicx::HttpMethod::kGet, "/slow",
        [](std::shared_ptr<quicx::IRequest> req, std::shared_ptr<quicx::IResponse> resp) {
            RequestTracker tracker;
            quicx::common::Metrics::CounterInc(custom_requests_total);

            std::cout << " Request: GET /slow (simulating 500ms processing)" << std::endl;

            // Simulate slow processing
            std::this_thread::sleep_for(std::chrono::milliseconds(500));

            resp->AppendBody("Slow response completed");
            resp->SetStatusCode(200);
        });

    // Handler 3: Error endpoint
    server->AddHandler(quicx::HttpMethod::kGet, "/error",
        [](std::shared_ptr<quicx::IRequest> req, std::shared_ptr<quicx::IResponse> resp) {
            RequestTracker tracker;
            quicx::common::Metrics::CounterInc(custom_requests_total);
            quicx::common::Metrics::CounterInc(custom_error_count);

            std::cout << " Request: GET /error (returning error)" << std::endl;

            resp->AppendBody("Internal Server Error");
            resp->SetStatusCode(500);
        });

    // Handler 4: Metrics endpoint (Prometheus format)
    server->AddHandler(quicx::HttpMethod::kGet, "/metrics",
        [](std::shared_ptr<quicx::IRequest> req, std::shared_ptr<quicx::IResponse> resp) {
            std::cout << " Request: GET /metrics (exporting Prometheus format)" << std::endl;

            std::string metrics_output = quicx::common::Metrics::ExportPrometheus();

            std::unordered_map<std::string, std::string> headers;
            headers["Content-Type"] = "text/plain; version=0.0.4";
            resp->SetHeaders(headers);
            resp->AppendBody(metrics_output);
            resp->SetStatusCode(200);
        });

    // Handler 5: Metrics dashboard (human-readable)
    server->AddHandler(quicx::HttpMethod::kGet, "/dashboard",
        [](std::shared_ptr<quicx::IRequest> req, std::shared_ptr<quicx::IResponse> resp) {
            std::cout << " Request: GET /dashboard (human-readable metrics)" << std::endl;

            std::string metrics_output = quicx::common::Metrics::ExportPrometheus();

            // Create a simple HTML dashboard
            std::ostringstream html;
            html << "<!DOCTYPE html>\n"
                 << "<html><head><title>quicX Metrics Dashboard</title>\n"
                 << "<style>\n"
                 << "body { font-family: monospace; margin: 20px; background: #f5f5f5; }\n"
                 << "h1 { color: #333; }\n"
                 << "pre { background: white; padding: 15px; border-radius: 5px; overflow-x: auto; }\n"
                 << ".metric { margin: 10px 0; padding: 10px; background: white; border-left: 4px solid #4CAF50; }\n"
                 << "</style>\n"
                 << "</head><body>\n"
                 << "<h1> quicX Metrics Dashboard</h1>\n"
                 << "<p>Last updated: " << std::time(nullptr) << "</p>\n"
                 << "<h2>Prometheus Format Export:</h2>\n"
                 << "<pre>" << metrics_output << "</pre>\n"
                 << "</body></html>";

            std::unordered_map<std::string, std::string> headers;
            headers["Content-Type"] = "text/html";
            resp->SetHeaders(headers);
            resp->AppendBody(html.str());
            resp->SetStatusCode(200);
        });

    quicx::Http3ServerConfig config;
    config.quic_config_.cert_pem_ = cert_pem;
    config.quic_config_.key_pem_ = key_pem;
    config.quic_config_.config_.worker_thread_num_ = 2;
    config.quic_config_.config_.log_level_ = quicx::LogLevel::kDebug;

    server->Init(config);

    std::cout << "   Server configured\n" << std::endl;

    // Step 4: Start Server
    std::cout << " Step 4: Starting server on 0.0.0.0:7010..." << std::endl;
    if (!server->Start("0.0.0.0", 7010)) {
        std::cerr << " Failed to start server" << std::endl;
        return 1;
    }
    std::cout << "   Server started successfully\n" << std::endl;

    // Step 5: Display Usage Information
    std::cout << "\n" << std::string(80, '=') << std::endl;
    std::cout << " Server is running! Available endpoints:" << std::endl;
    std::cout << std::string(80, '=') << std::endl;
    std::cout << "\n   https://localhost:7010/hello" << std::endl;
    std::cout << "     -> Simple hello endpoint\n" << std::endl;

    std::cout << "   https://localhost:7010/slow" << std::endl;
    std::cout << "     -> Slow endpoint (500ms delay)\n" << std::endl;

    std::cout << "   https://localhost:7010/error" << std::endl;
    std::cout << "     -> Error endpoint (returns 500)\n" << std::endl;

    std::cout << "   https://localhost:7010/metrics" << std::endl;
    std::cout << "     -> Prometheus metrics export\n" << std::endl;

    std::cout << "   https://localhost:7010/dashboard" << std::endl;
    std::cout << "     -> Human-readable metrics dashboard\n" << std::endl;

    std::cout << "\n Try these commands:" << std::endl;
    std::cout << "   curl -k https://localhost:7010/hello" << std::endl;
    std::cout << "   curl -k https://localhost:7010/metrics" << std::endl;
    std::cout << "   curl -k https://localhost:7010/dashboard > dashboard.html\n" << std::endl;

    std::cout << " Metrics will be printed every 10 seconds..." << std::endl;
    std::cout << "   Press Ctrl+C to stop\n" << std::endl;
    std::cout << std::string(80, '=') << "\n" << std::endl;

    // Step 6: Periodic Metrics Display
    std::thread metrics_thread([&]() {
        while (true) {
            std::this_thread::sleep_for(std::chrono::seconds(10));
            PrintMetricsSummary();
        }
    });

    // Join server (blocks until shutdown)
    server->Join();

    return 0;
}
