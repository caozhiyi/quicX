#include <chrono>
#include <iostream>
#include <memory>
#include <thread>

#include "http3/include/if_request.h"
#include "http3/include/if_response.h"
#include "http3/include/if_server.h"

class ErrorHandlingServer {
private:
    std::shared_ptr<quicx::IServer> server_;

public:
    ErrorHandlingServer() { server_ = quicx::IServer::Create(); }

    bool Init(uint16_t port) {
        // Use embedded certificates for demo
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

        quicx::Http3ServerConfig config;
        config.cert_pem_ = cert_pem;
        config.key_pem_ = key_pem;
        config.config_.thread_num_ = 4;
        config.config_.log_level_ = quicx::LogLevel::kInfo;

        if (!server_->Init(config)) {
            std::cerr << "Failed to initialize server" << std::endl;
            return false;
        }

        RegisterHandlers();

        std::cout << "Error Handling Demo Server started on port " << port << std::endl;
        std::cout << "Available endpoints:" << std::endl;
        std::cout << "  /normal  - Normal successful response" << std::endl;
        std::cout << "  /timeout - Slow response (10s delay)" << std::endl;
        std::cout << "  /error   - HTTP 500 error" << std::endl;
        std::cout << "  /large   - Large response" << std::endl;

        return true;
    }

    void RegisterHandlers() {
        // Normal endpoint
        server_->AddHandler(quicx::HttpMethod::kGet, "/normal",
            [](std::shared_ptr<quicx::IRequest> req, std::shared_ptr<quicx::IResponse> resp) {
                std::cout << "Handling /normal request" << std::endl;
                resp->SetStatusCode(200);
                resp->AppendBody("Success: Normal response");
            });

        // Timeout endpoint - simulates slow response
        server_->AddHandler(quicx::HttpMethod::kGet, "/timeout",
            [](std::shared_ptr<quicx::IRequest> req, std::shared_ptr<quicx::IResponse> resp) {
                std::cout << "Handling /timeout request - delaying 10s..." << std::endl;
                std::this_thread::sleep_for(std::chrono::seconds(10));
                resp->SetStatusCode(200);
                resp->AppendBody("Delayed response");
            });

        // Error endpoint - returns HTTP 500
        server_->AddHandler(quicx::HttpMethod::kGet, "/error",
            [](std::shared_ptr<quicx::IRequest> req, std::shared_ptr<quicx::IResponse> resp) {
                std::cout << "Handling /error request - returning 500" << std::endl;
                resp->SetStatusCode(500);
                resp->AppendBody("Simulated server error");
            });

        // Large response endpoint
        server_->AddHandler(quicx::HttpMethod::kGet, "/large",
            [](std::shared_ptr<quicx::IRequest> req, std::shared_ptr<quicx::IResponse> resp) {
                std::cout << "Handling /large request" << std::endl;

                // Generate 1MB response
                std::string large_body(1024 * 1024, 'X');

                resp->SetStatusCode(200);
                resp->AddHeader("Content-Type", "text/plain");
                resp->AppendBody(large_body);
            });

        // 404 handler for unknown paths
        server_->SetErrorHandler(
            [](std::string unique_id, uint32_t error) { std::cout << "Error handler called: " << error << std::endl; });
    }

    void Run(uint16_t port) {
        if (!server_->Start("0.0.0.0", port)) {
            std::cerr << "Failed to start server" << std::endl;
            return;
        }

        std::cout << "Press Ctrl+C to stop..." << std::endl;
        server_->Join();
    }
};

int main(int argc, char* argv[]) {
    uint16_t port = 8443;

    if (argc > 1) {
        port = static_cast<uint16_t>(std::atoi(argv[1]));
    }

    ErrorHandlingServer server;

    if (!server.Init(port)) {
        return 1;
    }

    server.Run(port);

    return 0;
}
