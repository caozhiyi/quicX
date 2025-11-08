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
#include <fstream>
#include <memory>
#include <chrono>
#include <signal.h>
#include <cstdio>
#include "http3/include/if_server.h"
#include "http3/include/if_request.h"
#include "http3/include/if_response.h"
#include "http3/include/if_async_handler.h"

using namespace quicx::http3;

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
            response->SetBody("Failed to open file for writing");
            return;
        }
        
        // Set response (will be sent immediately, before body chunks arrive)
        response->SetStatusCode(200);
        response->AddHeader("Content-Type", "application/json");
        response->SetBody("{\"status\": \"upload started\"}");
        
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
        response->SetBody("Missing filename parameter");
        return;
    }
    
    std::cout << "[Download] Requested file: " << filename << std::endl;
    
    // Open file for reading
    FILE* file = fopen(filename.c_str(), "rb");
    if (!file) {
        std::cerr << "[Download] File not found: " << filename << std::endl;
        response->SetStatusCode(404);
        response->SetBody("File not found");
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
    response->SetBody(
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
    // Parse arguments
    std::string addr = "0.0.0.0";
    uint16_t port = 8443;
    std::string cert_file = "server-cert.pem";
    std::string key_file = "server-key.pem";
    
    if (argc >= 2) port = std::atoi(argv[1]);
    if (argc >= 3) cert_file = argv[2];
    if (argc >= 4) key_file = argv[3];
    
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
    config.cert_file_ = cert_file;
    config.key_file_ = key_file;
    config.config_.log_level_ = LogLevel::kInfo;
    
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

