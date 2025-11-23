/**
 * @file server.cpp
 * @brief HTTP/3 Streaming API Server Example
 * 
 * This example demonstrates:
 * - Using IAsyncServerHandler for streaming request body reception
 * - Using body_provider for streaming response body transmission
 * - Handling large file uploads/downloads without buffering entire content
 */

#include <iostream>
#include <memory>
#include <chrono>
#include <signal.h>
#include <cstdio>
#include "http3/include/if_server.h"
#include "http3/include/if_request.h"
#include "http3/include/if_response.h"
#include "http3/include/if_async_handler.h"

using namespace quicx;

// Global server instance for signal handling
std::shared_ptr<IServer> g_server;

void SignalHandler(int signum) {
    std::cout << "\nReceived signal " << signum << ", shutting down..." << std::endl;
    if (g_server) {
        g_server->Stop();
    }
}

/**
 * @brief Async handler for file upload (streaming request body)
 * 
 * This handler demonstrates how to receive large file uploads incrementally
 * without buffering the entire file in memory.
 */
class FileUploadHandler : public IAsyncServerHandler {
public:
    void OnHeaders(std::shared_ptr<IRequest> request, 
                   std::shared_ptr<IResponse> response) {
        std::cout << "[Upload] Headers received" << std::endl;
        
        // Get filename from path parameter
        std::string filename = "uploaded_file.dat";
        auto& path_params = request->GetPathParams();
        auto it = path_params.find("filename");
        if (it != path_params.end()) {
            filename = it->second;
        }
        
        std::cout << "[Upload] Uploading to: " << filename << std::endl;
        
        // Open file for writing
        file_ = fopen(filename.c_str(), "wb");
        if (!file_) {
            std::cerr << "[Upload] Failed to open file: " << filename << std::endl;
            response->SetStatusCode(500);
            response->AppendBody("Failed to open file for writing");
            return;
        }
        
        // Set response (will be sent immediately, before body chunks arrive)
        response->SetStatusCode(200);
        response->AddHeader("Content-Type", "application/json");
        response->AppendBody("{\"status\": \"upload started\"}");
        
        bytes_received_ = 0;
        start_time_ = std::chrono::steady_clock::now();
    }
    
    void OnBodyChunk(const uint8_t* data, size_t length, bool is_last) {
        // Write chunk to file
        if (file_ && length > 0) {
            size_t written = fwrite(data, 1, length, file_);
            bytes_received_ += written;
            
            if (written != length) {
                std::cerr << "[Upload] Write error: expected " << length 
                         << " bytes, wrote " << written << std::endl;
            }
        }
        
        if (is_last) {
            // Close file and print statistics
            if (file_) {
                fclose(file_);
                file_ = nullptr;
            }
            
            auto end_time = std::chrono::steady_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                end_time - start_time_).count();
            
            double speed_mbps = bytes_received_ * 8.0 / duration / 1000.0;
            
            std::cout << "[Upload] Completed:" << std::endl;
            std::cout << "  - Total bytes: " << bytes_received_ << std::endl;
            std::cout << "  - Duration: " << duration << " ms" << std::endl;
            std::cout << "  - Speed: " << speed_mbps << " Mbps" << std::endl;
        }
    }
    
    ~FileUploadHandler() {
        if (file_) {
            fclose(file_);
        }
    }

private:
    FILE* file_ = nullptr;
    size_t bytes_received_ = 0;
    std::chrono::steady_clock::time_point start_time_;
};

/**
 * @brief Handler for file download (streaming response body)
 * 
 * This handler demonstrates how to send large file downloads incrementally
 * using body_provider.
 */
void HandleFileDownload(std::shared_ptr<IRequest> request, 
                       std::shared_ptr<IResponse> response) {
    std::string filename;
    auto& path_params = request->GetPathParams();
    auto it = path_params.find("filename");
    if (it != path_params.end()) {
        filename = it->second;
    }
    
    if (filename.empty()) {
        response->SetStatusCode(400);
        response->AppendBody("Missing filename parameter");
        return;
    }
    
    std::cout << "[Download] Requested file: " << filename << std::endl;
    
    // Open file for reading
    FILE* file = fopen(filename.c_str(), "rb");
    if (!file) {
        std::cerr << "[Download] File not found: " << filename << std::endl;
        response->SetStatusCode(404);
        response->AppendBody("File not found");
        return;
    }
    
    // Get file size
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);
    
    std::cout << "[Download] File size: " << file_size << " bytes" << std::endl;
    
    // Set response headers
    response->SetStatusCode(200);
    response->AddHeader("Content-Type", "application/octet-stream");
    response->AddHeader("Content-Length", std::to_string(file_size));
    
    // Set body provider for streaming
    // Note: We need to ensure the file handle remains valid during streaming
    // In a real application, use shared_ptr or proper RAII
    response->SetResponseBodyProvider(
        [file](uint8_t* buffer, size_t buffer_size) -> size_t {
            size_t bytes_read = fread(buffer, 1, buffer_size, file);
            
            if (bytes_read == 0) {
                // End of file - close it
                fclose(file);
                std::cout << "[Download] Completed" << std::endl;
            }
            
            return bytes_read;
        });
}

/**
 * @brief Simple complete mode handler for status endpoint
 */
void HandleStatus(std::shared_ptr<IRequest> request, 
                 std::shared_ptr<IResponse> response) {
    std::cout << "[Status] Request received" << std::endl;
    
    response->SetStatusCode(200);
    response->AddHeader("Content-Type", "application/json");
    response->AppendBody(
        "{\n"
        "  \"server\": \"quicX HTTP/3 Streaming Demo\",\n"
        "  \"version\": \"1.0\",\n"
        "  \"endpoints\": {\n"
        "    \"upload\": \"POST /upload/:filename\",\n"
        "    \"download\": \"GET /download/:filename\",\n"
        "    \"status\": \"GET /status\"\n"
        "  }\n"
        "}"
    );
}

int main(int argc, char* argv[]) {
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
    // Parse arguments
    std::string addr = "0.0.0.0";
    uint16_t port = 8443;
    // Setup signal handlers
    signal(SIGINT, SignalHandler);
    signal(SIGTERM, SignalHandler);
    
    // Create server
    Http3Settings settings;
    settings.max_concurrent_streams = 100;
    settings.max_header_list_size = 1024;
    
    g_server = IServer::Create(settings);
    if (!g_server) {
        std::cerr << "Failed to create server" << std::endl;
        return 1;
    }
    
    // Configure server
    Http3ServerConfig config;
    config.cert_pem_ = cert_pem;
    config.key_pem_ = key_pem;
    config.config_.thread_num_ = 1;
    config.config_.log_level_ = LogLevel::kDebug;
    
    if (!g_server->Init(config)) {
        std::cerr << "Failed to initialize server" << std::endl;
        return 1;
    }
    
    // Register handlers
    
    // Complete mode: Simple status endpoint
    g_server->AddHandler(HttpMethod::kGet, "/status", HandleStatus);
    
    // Async mode: File upload with streaming request body
    g_server->AddHandler(HttpMethod::kPost, "/upload/:filename", 
                        std::make_shared<FileUploadHandler>());
    
    // Complete mode with body provider: File download with streaming response body
    g_server->AddHandler(HttpMethod::kGet, "/download/:filename", 
                        HandleFileDownload);
    
    // Start server
    std::cout << "========================================" << std::endl;
    std::cout << "HTTP/3 Streaming API Server" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Listening on: " << addr << ":" << port << std::endl;
    std::cout << "" << std::endl;
    std::cout << "Endpoints:" << std::endl;
    std::cout << "  GET  /status                - Server status" << std::endl;
    std::cout << "  POST /upload/:filename      - Upload file (async mode)" << std::endl;
    std::cout << "  GET  /download/:filename    - Download file (complete mode with provider)" << std::endl;
    std::cout << "" << std::endl;
    std::cout << "Press Ctrl+C to stop" << std::endl;
    std::cout << "========================================" << std::endl;
    
    if (!g_server->Start(addr, port)) {
        std::cerr << "Failed to start server" << std::endl;
        return 1;
    }
    
    g_server->Join();
    
    std::cout << "Server stopped" << std::endl;
    return 0;
}

