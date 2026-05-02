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

// Calculate simple checksum of a file
std::string CalculateChecksum(const std::string& filepath) {
    std::ifstream file(filepath, std::ios::binary);
    if (!file) {
        return "";
    }

    std::hash<std::string> hasher;
    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    return std::to_string(hasher(content));
}

// Streaming file upload handler
class FileUploadHandler: public quicx::IAsyncServerHandler {
private:
    std::string root_directory_;
    std::string filepath_;
    std::ofstream file_;
    uint64_t bytes_received_ = 0;
    std::shared_ptr<quicx::IResponse> response_;

public:
    explicit FileUploadHandler(const std::string& root_dir):
        root_directory_(root_dir) {}

    void OnHeaders(std::shared_ptr<quicx::IRequest> request, std::shared_ptr<quicx::IResponse> response) override {
        response_ = response;
        bytes_received_ = 0;  // Reset for each new upload request
        std::string path = request->GetPath();

        // Remove leading /upload/
        if (path.find("/upload/") == 0) {
            path = path.substr(8);
        } else if (path.front() == '/') {
            path = path.substr(1);
        }

        // Construct full file path
        fs::path full_path = fs::path(root_directory_) / path;

        // Security check - prevent directory traversal
        auto canonical_root = fs::canonical(root_directory_);
        auto canonical_file = fs::weakly_canonical(full_path);

        if (canonical_file.string().find(canonical_root.string()) != 0) {
            response->SetStatusCode(403);
            response->AppendBody("Forbidden: Access denied");
            return;
        }

        // Create parent directories if needed
        if (full_path.has_parent_path()) {
            fs::create_directories(full_path.parent_path());
        }

        filepath_ = full_path.string();
        file_.open(filepath_, std::ios::binary | std::ios::trunc);

        if (!file_) {
            response->SetStatusCode(500);
            response->AppendBody("Failed to create file");
            return;
        }

        std::cout << "Upload started: " << filepath_ << std::endl;

        // Don't set response yet - wait until upload completes
    }

    void OnBodyChunk(const uint8_t* data, size_t length, bool is_last) override {
        if (file_.is_open() && length > 0) {
            file_.write(reinterpret_cast<const char*>(data), length);
            bytes_received_ += length;
        }

        if (is_last) {
            if (file_.is_open()) {
                file_.close();
                std::cout << "Upload completed: " << filepath_ << " (" << bytes_received_ << " bytes)" << std::endl;

                // Calculate checksum
                std::string checksum = CalculateChecksum(filepath_);

                response_->SetStatusCode(200);
                response_->AddHeader("X-Checksum", checksum);
                response_->AddHeader("X-Bytes-Received", std::to_string(bytes_received_));
                response_->AppendBody("Upload successful: " + std::to_string(bytes_received_) + " bytes");
            }
        }
    }

    void OnError(uint32_t error_code) override {
        std::cerr << "Upload error: protocol/network error code " << error_code << std::endl;
        if (file_.is_open()) {
            file_.close();
        }
    }
};

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
        config.quic_config_.config_.worker_thread_num_ = 4;
        config.quic_config_.config_.log_level_ = quicx::LogLevel::kDebug;

        // Load certificates
        config.quic_config_.cert_pem_ = cert_pem;
        config.quic_config_.key_pem_ = key_pem;

        if (!server_->Init(config)) {
            std::cerr << "Failed to initialize server" << std::endl;
            return false;
        }

        // Register streaming upload handler (POST /upload/*)
        server_->AddHandler(
            quicx::HttpMethod::kPost, "/upload/*", std::make_shared<FileUploadHandler>(root_directory_));

        // Register file download handler (GET /*)
        // Use streaming response via body provider
        server_->AddHandler(quicx::HttpMethod::kGet, "/*",
            [this](std::shared_ptr<quicx::IRequest> req, std::shared_ptr<quicx::IResponse> resp) {
                HandleFileDownload(req, resp);
            });

        std::cout << "File Transfer Server started" << std::endl;
        std::cout << "Serving files from: " << root_directory_ << std::endl;
        std::cout << "Endpoints:" << std::endl;
        std::cout << "  GET  /*         - Download file (streaming)" << std::endl;
        std::cout << "  POST /upload/*  - Upload file (streaming)" << std::endl;

        return true;
    }

    void HandleFileDownload(std::shared_ptr<quicx::IRequest> req, std::shared_ptr<quicx::IResponse> resp) {
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

        // Calculate content length
        uint64_t content_length = end_byte - start_byte + 1;

        // Set response headers
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

        std::cout << "Download started: " << path << " (" << start_byte << "-" << end_byte << "/" << file_size << ")"
                  << std::endl;

        // Use streaming response via body provider
        std::string filepath_str = filepath.string();
        uint64_t remaining = content_length;
        uint64_t current_pos = start_byte;

        // Open file for streaming - capture by value
        auto file_ptr = std::make_shared<std::ifstream>(filepath_str, std::ios::binary);
        if (!file_ptr->is_open()) {
            resp->SetStatusCode(500);
            resp->AppendBody("Failed to open file");
            return;
        }
        file_ptr->seekg(start_byte);

        // Set body provider for streaming response
        resp->SetResponseBodyProvider([file_ptr, remaining, path](uint8_t* buf, size_t size) mutable -> size_t {
            if (remaining == 0 || !file_ptr->is_open() || file_ptr->eof()) {
                if (file_ptr->is_open()) {
                    file_ptr->close();
                    std::cout << "Download completed: " << path << std::endl;
                }
                return 0;
            }

            size_t to_read = std::min(static_cast<size_t>(remaining), size);
            file_ptr->read(reinterpret_cast<char*>(buf), to_read);
            size_t actually_read = file_ptr->gcount();

            remaining -= actually_read;
            return actually_read;
        });
    }

    void Run() {
        server_->Start("0.0.0.0", 7006);

        std::cout << "Press Ctrl+C to stop..." << std::endl;

        // Keep running
        while (true) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }
};

int main(int argc, char* argv[]) {
    std::string root_dir = "./files";

    if (argc > 1) {
        root_dir = argv[1];
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
