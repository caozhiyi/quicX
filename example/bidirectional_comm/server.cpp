#include <atomic>
#include <iostream>
#include <memory>
#include <mutex>
#include <unordered_map>

#include "http3/include/if_request.h"
#include "http3/include/if_response.h"
#include "http3/include/if_server.h"

class BidirectionalServer {
private:
    std::shared_ptr<quicx::IServer> server_;
    std::unordered_map<std::string, std::chrono::steady_clock::time_point> clients_;
    std::mutex clients_mutex_;
    std::atomic<int> message_count_{0};

public:
    BidirectionalServer() { server_ = quicx::IServer::Create(); }

    bool Init(uint16_t port) {
        // Use embedded certificates
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
        config.config_.log_level_ = quicx::LogLevel::kDebug;

        if (!server_->Init(config)) {
            std::cerr << "Failed to initialize server" << std::endl;
            return false;
        }

        RegisterHandlers();

        std::cout << "Bidirectional Server started on port " << port << std::endl;
        std::cout << "Endpoints:" << std::endl;
        std::cout << "  /connect   - Client connection" << std::endl;
        std::cout << "  /message   - Receive messages" << std::endl;
        std::cout << "  /heartbeat - Heartbeat check" << std::endl;

        return true;
    }

    void RegisterHandlers() {
        // Connection endpoint
        server_->AddHandler(quicx::HttpMethod::kGet, "/connect",
            [this](std::shared_ptr<quicx::IRequest> req, std::shared_ptr<quicx::IResponse> resp) {
                std::string client_id = "client_" + std::to_string(std::rand());

                {
                    std::lock_guard<std::mutex> lock(clients_mutex_);
                    clients_[client_id] = std::chrono::steady_clock::now();
                }

                std::cout << "New client connected: " << client_id << std::endl;

                resp->SetStatusCode(200);
                resp->AppendBody("Connected: " + client_id);
            });

        // Message endpoint
        server_->AddHandler(quicx::HttpMethod::kPost, "/message",
            [this](std::shared_ptr<quicx::IRequest> req, std::shared_ptr<quicx::IResponse> resp) {
                std::string message = req->GetBodyAsString();
                int count = ++message_count_;

                std::cout << "Message #" << count << ": " << message << std::endl;

                resp->SetStatusCode(200);
                resp->AppendBody("Message received: " + message + "\nEcho: " + message);
            });

        // Heartbeat endpoint
        server_->AddHandler(quicx::HttpMethod::kGet, "/heartbeat",
            [](std::shared_ptr<quicx::IRequest> req, std::shared_ptr<quicx::IResponse> resp) {
                resp->SetStatusCode(200);
                resp->AppendBody("alive");
            });
    }

    void Run(uint16_t port) {
        if (!server_->Start("0.0.0.0", port)) {
            std::cerr << "Failed to start server" << std::endl;
            return;
        }

        std::cout << "Press Ctrl+C to stop..." << std::endl;
        server_->Join();
    }

    void PrintStats() {
        std::lock_guard<std::mutex> lock(clients_mutex_);
        std::cout << "\nServer Statistics:" << std::endl;
        std::cout << "  Active clients: " << clients_.size() << std::endl;
        std::cout << "  Total messages: " << message_count_.load() << std::endl;
    }
};

int main(int argc, char* argv[]) {
    uint16_t port = 8443;

    if (argc > 1) {
        port = static_cast<uint16_t>(std::atoi(argv[1]));
    }

    std::cout << "╔════════════════════════════════════════╗" << std::endl;
    std::cout << "║  Bidirectional Communication Server   ║" << std::endl;
    std::cout << "╚════════════════════════════════════════╝\n" << std::endl;

    BidirectionalServer server;

    if (!server.Init(port)) {
        return 1;
    }

    server.Run(port);

    return 0;
}
