#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

#include "http3/include/if_request.h"
#include "http3/include/if_response.h"
#include "http3/include/if_server.h"

namespace fs = std::filesystem;

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

// Calculate SHA-256 checksum of a file
std::string CalculateChecksum(const std::string& filepath) {
    // For demo purposes, return a simple hash
    // In production, use a proper SHA-256 implementation
    std::ifstream file(filepath, std::ios::binary);
    if (!file) {
        return "";
    }

    std::hash<std::string> hasher;
    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    return std::to_string(hasher(content));
}

class FileTransferServer {
private:
    std::shared_ptr<quicx::IServer> server_;
    std::string root_directory_;

public:
    FileTransferServer(const std::string& root_dir):
        root_directory_(root_dir) {
        server_ = quicx::IServer::Create();
    }

    bool Init() {
        quicx::Http3ServerConfig config;
        config.config_.thread_num_ = 4;
        config.config_.log_level_ = quicx::LogLevel::kInfo;

        // Load certificates
        config.cert_pem_ = cert_pem;
        config.key_pem_ = key_pem;

        if (!server_->Init(config)) {
            std::cerr << "Failed to initialize server" << std::endl;
            return false;
        }

        // Register file download handler
        server_->AddHandler(quicx::HttpMethod::kGet, "/*",
            [this](std::shared_ptr<quicx::IRequest> req, std::shared_ptr<quicx::IResponse> resp) {
                HandleFileRequest(req, resp);
            });

        std::cout << "Serving files from: " << root_directory_ << std::endl;

        return true;
    }

    void HandleFileRequest(std::shared_ptr<quicx::IRequest> req, std::shared_ptr<quicx::IResponse> resp) {
        std::string path = req->GetPath();

        // Remove leading slash
        if (path.front() == '/') {
            path = path.substr(1);
        }

        // Construct full file path
        fs::path filepath = fs::path(root_directory_) / path;

        // Security check - prevent directory traversal
        auto canonical_root = fs::canonical(root_directory_);
        auto canonical_file = fs::weakly_canonical(filepath);

        if (canonical_file.string().find(canonical_root.string()) != 0) {
            resp->SetStatusCode(403);
            resp->AppendBody("Forbidden: Access denied");
            return;
        }

        // Check if file exists
        if (!fs::exists(filepath) || !fs::is_regular_file(filepath)) {
            resp->SetStatusCode(404);
            resp->AppendBody("File not found");
            return;
        }

        // Get file size
        auto file_size = fs::file_size(filepath);

        // Check for Range header (resume support)
        std::string range_header;
        req->GetHeader("Range", range_header);
        uint64_t start_byte = 0;
        uint64_t end_byte = file_size - 1;
        bool is_partial = false;

        if (!range_header.empty()) {
            // Parse Range header: "bytes=start-end"
            if (range_header.find("bytes=") == 0) {
                std::string range = range_header.substr(6);
                size_t dash_pos = range.find('-');

                if (dash_pos != std::string::npos) {
                    std::string start_str = range.substr(0, dash_pos);
                    std::string end_str = range.substr(dash_pos + 1);

                    if (!start_str.empty()) {
                        start_byte = std::stoull(start_str);
                    }
                    if (!end_str.empty()) {
                        end_byte = std::stoull(end_str);
                    }

                    is_partial = true;
                }
            }
        }

        // Validate range
        if (start_byte >= file_size || end_byte >= file_size || start_byte > end_byte) {
            resp->SetStatusCode(416);  // Range Not Satisfiable
            resp->AddHeader("Content-Range", "bytes */" + std::to_string(file_size));
            return;
        }

        // Open file
        std::ifstream file(filepath.string(), std::ios::binary);
        if (!file) {
            resp->SetStatusCode(500);
            resp->AppendBody("Failed to open file");
            return;
        }

        // Seek to start position
        file.seekg(start_byte);

        // Calculate content length
        uint64_t content_length = end_byte - start_byte + 1;

        // Read file content
        std::vector<char> buffer(content_length);
        file.read(buffer.data(), content_length);

        // Set response
        if (is_partial) {
            resp->SetStatusCode(206);  // Partial Content
            resp->AddHeader("Content-Range", "bytes " + std::to_string(start_byte) + "-" + std::to_string(end_byte) +
                                                 "/" + std::to_string(file_size));
        } else {
            resp->SetStatusCode(200);
        }

        resp->AddHeader("Content-Length", std::to_string(content_length));
        resp->AddHeader("Accept-Ranges", "bytes");
        resp->AddHeader("Content-Type", "application/octet-stream");

        // Add checksum header
        std::string checksum = CalculateChecksum(filepath.string());
        if (!checksum.empty()) {
            resp->AddHeader("X-Checksum", checksum);
        }

        // Set body
        resp->AppendBody(std::string(buffer.begin(), buffer.end()));

        std::cout << "Served: " << path << " (" << start_byte << "-" << end_byte << "/" << file_size << ")"
                  << std::endl;
    }

    void Run() {
        server_->Start("0.0.0.0", 8443);

        std::cout << "Press Ctrl+C to stop..." << std::endl;

        // Keep running
        while (true) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }
};

int main(int argc, char* argv[]) {
    std::string root_dir = "./files";

    if (argc > 2) {
        root_dir = argv[2];
    }

    // Create root directory if it doesn't exist
    if (!fs::exists(root_dir)) {
        fs::create_directories(root_dir);
        std::cout << "Created directory: " << root_dir << std::endl;
    }

    FileTransferServer server(root_dir);

    if (!server.Init()) {
        return 1;
    }

    server.Run();

    return 0;
}
