/**
 * @file client.cpp
 * @brief HTTP/3 Streaming API Client Example
 * 
 * This example demonstrates:
 * - Using IAsyncClientHandler for streaming response body reception
 * - Using body_provider for streaming request body transmission
 * - Handling large file uploads/downloads without buffering entire content
 */

#include <iostream>
#include <fstream>
#include <memory>
#include <thread>
#include <chrono>
#include <cstdio>
#include "http3/include/if_client.h"
#include "http3/include/if_request.h"
#include "http3/include/if_response.h"
#include "http3/include/if_async_handler.h"

using namespace quicx::http3;

/**
 * @brief Async handler for file download (streaming response body)
 * 
 * This handler demonstrates how to receive large file downloads incrementally
 * without buffering the entire file in memory.
 */
class FileDownloadHandler:
    public IAsyncClientHandler {
public:
    explicit FileDownloadHandler(const std::string& output_filename) 
        : output_filename_(output_filename) {}
    
    void OnHeaders(std::shared_ptr<IResponse> response) {
        std::cout << "[Download] Response headers received" << std::endl;
        std::cout << "  - Status: " << response->GetStatusCode() << std::endl;
        
        std::string content_length;
        if (response->GetHeader("Content-Length", content_length)) {
            std::cout << "  - Content-Length: " << content_length << " bytes" << std::endl;
        }
        
        // Open file for writing
        if (response->GetStatusCode() == 200) {
            file_ = fopen(output_filename_.c_str(), "wb");
            if (!file_) {
                std::cerr << "[Download] Failed to open output file: " 
                         << output_filename_ << std::endl;
                return;
            }
            
            std::cout << "[Download] Saving to: " << output_filename_ << std::endl;
            bytes_received_ = 0;
            start_time_ = std::chrono::steady_clock::now();
        } else {
            std::cerr << "[Download] Request failed with status: " 
                     << response->GetStatusCode() << std::endl;
        }
    }
    
    void OnBodyChunk(const uint8_t* data, size_t length, bool is_last) {
        // Write chunk to file
        if (file_ && length > 0) {
            size_t written = fwrite(data, 1, length, file_);
            bytes_received_ += written;
            
            if (written != length) {
                std::cerr << "[Download] Write error: expected " << length 
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
            
            double speed_mbps = duration > 0 ? bytes_received_ * 8.0 / duration / 1000.0 : 0;
            
            std::cout << "[Download] Completed:" << std::endl;
            std::cout << "  - Total bytes: " << bytes_received_ << std::endl;
            std::cout << "  - Duration: " << duration << " ms" << std::endl;
            std::cout << "  - Speed: " << speed_mbps << " Mbps" << std::endl;
        }
    }
    
    ~FileDownloadHandler() {
        if (file_) {
            fclose(file_);
        }
    }

private:
    std::string output_filename_;
    FILE* file_ = nullptr;
    size_t bytes_received_ = 0;
    std::chrono::steady_clock::time_point start_time_;
};

/**
 * @brief Upload file using body provider (streaming request body)
 */
bool UploadFile(IClient* client, const std::string& url, 
                const std::string& input_filename) {
    std::cout << "[Upload] Uploading: " << input_filename << std::endl;
    
    // Open file for reading
    FILE* file = fopen(input_filename.c_str(), "rb");
    if (!file) {
        std::cerr << "[Upload] File not found: " << input_filename << std::endl;
        return false;
    }
    
    // Get file size
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);
    
    std::cout << "[Upload] File size: " << file_size << " bytes" << std::endl;
    
    // Create request
    auto request = IRequest::Create();
    request->AddHeader("Content-Type", "application/octet-stream");
    request->AddHeader("Content-Length", std::to_string(file_size));
    
    // Set body provider for streaming upload
    request->SetRequestBodyProvider(
        [file](uint8_t* buffer, size_t buffer_size) -> size_t {
            size_t bytes_read = fread(buffer, 1, buffer_size, file);
            
            if (bytes_read == 0) {
                // End of file - close it
                fclose(file);
                std::cout << "[Upload] Request body sent" << std::endl;
            }
            
            return bytes_read;
        });
    
    // Send request (complete mode for response)
    bool success = false;
    client->DoRequest(url, HttpMethod::kPost, request,
        [&success](std::shared_ptr<IResponse> response, uint32_t error) {
            if (error == 0) {
                std::cout << "[Upload] Response received:" << std::endl;
                std::cout << "  - Status: " << response->GetStatusCode() << std::endl;
                std::cout << "  - Body: " << response->GetBody() << std::endl;
                success = (response->GetStatusCode() == 200);
            } else {
                std::cerr << "[Upload] Request failed with error: " << error << std::endl;
            }
        });
    
    // Wait for response (in real application, use proper synchronization)
    std::this_thread::sleep_for(std::chrono::seconds(2));
    
    return success;
}

/**
 * @brief Download file using async handler (streaming response body)
 */
bool DownloadFile(IClient* client, const std::string& url, 
                  const std::string& output_filename) {
    std::cout << "[Download] Requesting: " << url << std::endl;
    
    auto request = IRequest::Create();
    auto handler = std::make_shared<FileDownloadHandler>(output_filename);
    
    // Send request with async handler for streaming response
    bool sent = client->DoRequest(url, HttpMethod::kGet, request, handler);
    
    if (!sent) {
        std::cerr << "[Download] Failed to send request" << std::endl;
        return false;
    }
    
    // Wait for download to complete (in real application, use proper synchronization)
    std::this_thread::sleep_for(std::chrono::seconds(2));
    
    return true;
}

/**
 * @brief Get server status (complete mode)
 */
void GetStatus(IClient* client, const std::string& url) {
    std::cout << "[Status] Requesting: " << url << std::endl;
    
    auto request = IRequest::Create();
    
    client->DoRequest(url, HttpMethod::kGet, request,
        [](std::shared_ptr<IResponse> response, uint32_t error) {
            if (error == 0) {
                std::cout << "[Status] Response:" << std::endl;
                std::cout << "  - Status: " << response->GetStatusCode() << std::endl;
                std::cout << "  - Body:" << std::endl;
                std::cout << response->GetBody() << std::endl;
            } else {
                std::cerr << "[Status] Request failed with error: " << error << std::endl;
            }
        });
    
    // Wait for response
    std::this_thread::sleep_for(std::chrono::seconds(1));
}

void PrintUsage(const char* program) {
    std::cout << "Usage: " << program << " <command> <server_url> [options]" << std::endl;
    std::cout << std::endl;
    std::cout << "Commands:" << std::endl;
    std::cout << "  status                          - Get server status" << std::endl;
    std::cout << "  upload <filename>               - Upload file to server" << std::endl;
    std::cout << "  download <filename> <output>    - Download file from server" << std::endl;
    std::cout << std::endl;
    std::cout << "Examples:" << std::endl;
    std::cout << "  " << program << " status https://localhost:8443/status" << std::endl;
    std::cout << "  " << program << " upload https://localhost:8443/upload/test.dat input.dat" << std::endl;
    std::cout << "  " << program << " download https://localhost:8443/download/test.dat output.dat" << std::endl;
}

int main(int argc, char* argv[]) {
    if (argc < 3) {
        PrintUsage(argv[0]);
        return 1;
    }
    
    std::string command = argv[1];
    std::string url = argv[2];
    
    // Create client
    Http3Settings settings;
    auto client = IClient::Create(settings);
    if (!client) {
        std::cerr << "Failed to create client" << std::endl;
        return 1;
    }
    
    // Configure client
    Http3Config config;
    config.log_level_ = LogLevel::kInfo;
    
    if (!client->Init(config)) {
        std::cerr << "Failed to initialize client" << std::endl;
        return 1;
    }
    
    std::cout << "========================================" << std::endl;
    std::cout << "HTTP/3 Streaming API Client" << std::endl;
    std::cout << "========================================" << std::endl;
    
    // Execute command
    if (command == "status") {
        GetStatus(client.get(), url);
    }
    else if (command == "upload") {
        if (argc < 4) {
            std::cerr << "Error: Upload requires filename argument" << std::endl;
            PrintUsage(argv[0]);
            return 1;
        }
        std::string filename = argv[3];
        if (!UploadFile(client.get(), url, filename)) {
            std::cerr << "Upload failed" << std::endl;
            return 1;
        }
    }
    else if (command == "download") {
        if (argc < 4) {
            std::cerr << "Error: Download requires output filename argument" << std::endl;
            PrintUsage(argv[0]);
            return 1;
        }
        std::string output_filename = argv[3];
        if (!DownloadFile(client.get(), url, output_filename)) {
            std::cerr << "Download failed" << std::endl;
            return 1;
        }
    }
    else {
        std::cerr << "Unknown command: " << command << std::endl;
        PrintUsage(argv[0]);
        return 1;
    }
    
    std::cout << "========================================" << std::endl;
    std::cout << "Done" << std::endl;
    
    return 0;
}

