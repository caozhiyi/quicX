/**
 * @file client.cpp
 * @brief HTTP/3 Streaming API Client Example
 *
 * This example demonstrates:
 * - Using IAsyncClientHandler for streaming response body reception
 * - Using body_provider for streaming request body transmission
 * - Handling large file uploads/downloads without buffering entire content
 */

#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "http3/include/if_async_handler.h"
#include "http3/include/if_client.h"
#include "http3/include/if_request.h"
#include "http3/include/if_response.h"

using namespace quicx;

/**
 * @brief Extract path from URL
 * @param url Full URL (e.g., "https://localhost:8443/status")
 * @return Path portion (e.g., "/status")
 */
std::string ExtractPathFromUrl(const std::string& url) {
    // Find the path start after "://"
    size_t scheme_end = url.find("://");
    if (scheme_end == std::string::npos) {
        return "/";
    }

    // Find the first '/' after the scheme
    size_t path_start = url.find('/', scheme_end + 3);
    if (path_start == std::string::npos) {
        return "/";
    }

    // Extract path (up to '?' if query string exists)
    size_t query_start = url.find('?', path_start);
    if (query_start != std::string::npos) {
        return url.substr(path_start, query_start - path_start);
    }

    return url.substr(path_start);
}

/**
 * @brief Determine operation type from URL path
 * @param path URL path (e.g., "/status", "/upload/test.dat", "/download/test.dat")
 * @return Operation type: "status", "upload", "download", or empty if unknown
 */
std::string GetOperationFromPath(const std::string& path) {
    if (path == "/status" || path.find("/status/") == 0) {
        return "status";
    } else if (path.find("/upload/") == 0 || path.find("/upload") == 0) {
        return "upload";
    } else if (path.find("/download/") == 0 || path.find("/download") == 0) {
        return "download";
    }
    return "";
}

/**
 * @brief Async handler for file download (streaming response body)
 *
 * This handler demonstrates how to receive large file downloads incrementally
 * without buffering the entire file in memory.
 */
class FileDownloadHandler: public IAsyncClientHandler {
public:
    explicit FileDownloadHandler(const std::string& output_filename):
        output_filename_(output_filename) {}

    // Wait for download to complete
    void WaitForCompletion() {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [this] { return completed_; });
    }

    void OnHeaders(std::shared_ptr<IResponse> response) {
        std::cout << "[Download] Response headers received" << std::endl;
        std::cout << "  - Status: " << response->GetStatusCode() << std::endl;

        // Get Content-Length if available for progress tracking
        std::string content_length;
        if (response->GetHeader("Content-Length", content_length)) {
            std::cout << "  - Content-Length: " << content_length << " bytes" << std::endl;
            try {
                total_size_ = std::stoull(content_length);
            } catch (...) {
                total_size_ = 0;  // Unknown size
            }
        } else {
            total_size_ = 0;  // Unknown size
        }

        // Open file for writing
        if (response->GetStatusCode() == 200) {
            file_ = fopen(output_filename_.c_str(), "wb");
            if (!file_) {
                std::cerr << "[Download] Failed to open output file: " << output_filename_ << std::endl;
                // Notify completion even on error
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    completed_ = true;
                }
                cv_.notify_one();
                return;
            }

            std::cout << "[Download] Saving to: " << output_filename_ << std::endl;
            bytes_received_ = 0;
            last_percent_ = -1;
            start_time_ = std::chrono::steady_clock::now();
        } else {
            std::cerr << "[Download] Request failed with status: " << response->GetStatusCode() << std::endl;
            // Notify completion on error status
            {
                std::lock_guard<std::mutex> lock(mutex_);
                completed_ = true;
            }
            cv_.notify_one();
        }
    }

    void OnBodyChunk(const uint8_t* data, size_t length, bool is_last) {
        // Write chunk to file
        if (file_ && length > 0) {
            size_t written = fwrite(data, 1, length, file_);
            bytes_received_ += written;

            if (written != length) {
                std::cerr << "[Download] Write error: expected " << length << " bytes, wrote " << written << std::endl;
            }

            // Calculate and display progress
            if (total_size_ > 0) {
                int percent = static_cast<int>((bytes_received_ * 100) / total_size_);

                // Clamp to 100% maximum
                if (percent > 100) {
                    percent = 100;
                }

                // Only update display when percentage changes (avoid too frequent updates)
                if (percent != last_percent_) {
                    std::cout << "\r[Download] Progress: " << percent << "%" << std::flush;
                    last_percent_ = percent;
                }
            }
        }

        if (is_last) {
            // Close file and print statistics
            if (file_) {
                fclose(file_);
                file_ = nullptr;
            }

            // Print final progress if we were tracking it
            if (total_size_ > 0 && last_percent_ < 100) {
                std::cout << "\r[Download] Progress: 100%" << std::endl;
            } else if (total_size_ > 0) {
                std::cout << std::endl;
            }

            auto end_time = std::chrono::steady_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time_).count();

            double speed_mbps = duration > 0 ? bytes_received_ * 8.0 / duration / 1000.0 : 0;

            std::cout << "[Download] Completed:" << std::endl;
            std::cout << "  - Total bytes: " << bytes_received_ << std::endl;
            std::cout << "  - Duration: " << duration << " ms" << std::endl;
            std::cout << "  - Speed: " << speed_mbps << " Mbps" << std::endl;

            // Notify completion
            {
                std::lock_guard<std::mutex> lock(mutex_);
                completed_ = true;
            }
            cv_.notify_one();
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
    size_t total_size_ = 0;  // Total expected size from Content-Length header
    int last_percent_ = -1;  // Last displayed percentage
    std::chrono::steady_clock::time_point start_time_;
    std::mutex mutex_;
    std::condition_variable cv_;
    bool completed_ = false;
};

/**
 * @brief Upload file using body provider (streaming request body)
 */
bool UploadFile(IClient* client, const std::string& url, const std::string& input_filename) {
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

    // Track upload progress
    size_t bytes_uploaded = 0;
    int last_percent = -1;

    // Synchronization for upload completion
    std::mutex upload_mutex;
    std::condition_variable upload_cv;
    bool request_body_sent = false;
    bool response_received = false;
    bool success = false;

    // Create request
    auto request = IRequest::Create();
    request->AddHeader("Content-Type", "application/octet-stream");
    request->AddHeader("Content-Length", std::to_string(file_size));

    // Set body provider for streaming upload
    request->SetRequestBodyProvider(
        [file, file_size, &bytes_uploaded, &last_percent, &upload_mutex, &upload_cv, &request_body_sent](uint8_t* buffer, size_t buffer_size) -> size_t {
            size_t bytes_read = fread(buffer, 1, buffer_size, file);

            if (bytes_read > 0) {
                bytes_uploaded += bytes_read;

                // Calculate and display progress
                if (file_size > 0) {
                    int percent = static_cast<int>((bytes_uploaded * 100) / file_size);

                    // Clamp to 100% maximum
                    if (percent > 100) {
                        percent = 100;
                    }

                    // Only update display when percentage changes (avoid too frequent updates)
                    if (percent != last_percent) {
                        std::cout << "\r[Upload] Progress: " << percent << "%" << std::flush;
                        last_percent = percent;
                    }
                }
            }

            if (bytes_read == 0) {
                // End of file - close it and print final progress
                fclose(file);
                if (last_percent < 100) {
                    std::cout << "\r[Upload] Progress: 100%" << std::endl;
                } else {
                    std::cout << std::endl;
                }
                std::cout << "[Upload] Request body sent" << std::endl;
                
                // Notify that request body is sent
                {
                    std::lock_guard<std::mutex> lock(upload_mutex);
                    request_body_sent = true;
                }
                upload_cv.notify_one();
            }

            return bytes_read;
        });

    // Send request (complete mode for response)
    client->DoRequest(url, HttpMethod::kPost, request, [&success, &upload_mutex, &upload_cv, &response_received](std::shared_ptr<IResponse> response, uint32_t error) {
        if (error == 0) {
            std::cout << "[Upload] Response received:" << std::endl;
            std::cout << "  - Status: " << response->GetStatusCode() << std::endl;
            std::cout << "  - Body: " << response->GetBodyAsString() << std::endl;
            success = (response->GetStatusCode() == 200);
        } else {
            std::cerr << "[Upload] Request failed with error: " << error << std::endl;
        }
        
        // Notify that response is received
        {
            std::lock_guard<std::mutex> lock(upload_mutex);
            response_received = true;
        }
        upload_cv.notify_one();
    });

    // Wait for both request body sent and response received
    std::unique_lock<std::mutex> lock(upload_mutex);
    upload_cv.wait(lock, [&request_body_sent, &response_received] { 
        return request_body_sent && response_received; 
    });

    return success;
}

/**
 * @brief Download file using async handler (streaming response body)
 */
bool DownloadFile(IClient* client, const std::string& url, const std::string& output_filename) {
    std::cout << "[Download] Requesting: " << url << std::endl;

    auto request = IRequest::Create();
    auto handler = std::make_shared<FileDownloadHandler>(output_filename);

    // Send request with async handler for streaming response
    bool sent = client->DoRequest(url, HttpMethod::kGet, request, handler);

    if (!sent) {
        std::cerr << "[Download] Failed to send request" << std::endl;
        return false;
    }

    // Wait for download to complete
    handler->WaitForCompletion();

    return true;
}

/**
 * @brief Get server status (complete mode)
 */
void GetStatus(IClient* client, const std::string& url) {
    std::cout << "[Status] Requesting: " << url << std::endl;

    auto request = IRequest::Create();

    client->DoRequest(url, HttpMethod::kGet, request, [](std::shared_ptr<IResponse> response, uint32_t error) {
        if (error == 0) {
            std::cout << "[Status] Response:" << std::endl;
            std::cout << "  - Status: " << response->GetStatusCode() << std::endl;
            std::cout << "  - Body:" << std::endl;
            std::cout << response->GetBodyAsString() << std::endl;
        } else {
            std::cerr << "[Status] Request failed with error: " << error << std::endl;
        }
    });

    // Wait for response
    std::this_thread::sleep_for(std::chrono::seconds(1));
}

void PrintUsage(const char* program) {
    std::cout << "Usage: " << program << " <server_url> [options]" << std::endl;
    std::cout << std::endl;
    std::cout << "The operation type is automatically detected from the URL path:" << std::endl;
    std::cout << "  /status                          - Get server status" << std::endl;
    std::cout << "  /upload/...                      - Upload file to server (requires local filename)" << std::endl;
    std::cout << "  /download/...                   - Download file from server (requires output filename)"
              << std::endl;
    std::cout << std::endl;
    std::cout << "Examples:" << std::endl;
    std::cout << "  " << program << " https://localhost:8443/status" << std::endl;
    std::cout << "  " << program << " https://localhost:8443/upload/test.dat input.dat" << std::endl;
    std::cout << "  " << program << " https://localhost:8443/download/test.dat output.dat" << std::endl;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        PrintUsage(argv[0]);
        return 1;
    }

    std::string url = argv[1];

    // Extract path from URL and determine operation type
    std::string path = ExtractPathFromUrl(url);
    std::string operation = GetOperationFromPath(path);

    if (operation.empty()) {
        std::cerr << "Error: Unable to determine operation from URL path: " << path << std::endl;
        std::cerr << "Supported paths: /status, /upload/..., /download/..." << std::endl;
        PrintUsage(argv[0]);
        return 1;
    }

    // Create client
    Http3Settings settings;
    auto client = IClient::Create(settings);
    if (!client) {
        std::cerr << "Failed to create client" << std::endl;
        return 1;
    }

    // Configure client
    Http3Config config;
    config.log_level_ = LogLevel::kError;

    if (!client->Init(config)) {
        std::cerr << "Failed to initialize client" << std::endl;
        return 1;
    }

    std::cout << "========================================" << std::endl;
    std::cout << "HTTP/3 Streaming API Client" << std::endl;
    std::cout << "========================================" << std::endl;

    // Execute operation based on URL path
    if (operation == "status") {
        GetStatus(client.get(), url);
    } else if (operation == "upload") {
        if (argc < 3) {
            std::cerr << "Error: Upload requires local filename argument" << std::endl;
            PrintUsage(argv[0]);
            return 1;
        }
        std::string filename = argv[2];
        if (!UploadFile(client.get(), url, filename)) {
            std::cerr << "Upload failed" << std::endl;
            return 1;
        }
    } else if (operation == "download") {
        if (argc < 3) {
            std::cerr << "Error: Download requires output filename argument" << std::endl;
            PrintUsage(argv[0]);
            return 1;
        }
        std::string output_filename = argv[2];
        if (!DownloadFile(client.get(), url, output_filename)) {
            std::cerr << "Download failed" << std::endl;
            return 1;
        }
    }

    std::cout << "========================================" << std::endl;
    std::cout << "Done" << std::endl;

    return 0;
}
