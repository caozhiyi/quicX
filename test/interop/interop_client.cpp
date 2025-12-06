/**
 * @file interop_client.cpp
 * @brief QUIC Interop Test Client for quicX
 *
 * This client is designed to work with the quic-interop-runner framework.
 * It downloads files over QUIC/HTTP3 and supports various test scenarios.
 *
 * Environment Variables:
 * - SERVER: Server hostname
 * - PORT: Server port (default: 443)
 * - REQUESTS: Space-separated URLs to download
 * - DOWNLOADS: Download directory (default: /downloads)
 * - QLOGDIR: Directory for qlog output
 * - SSLKEYLOGFILE: File for TLS key logging
 */

#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <cstdlib>
#include <fstream>
#include <thread>
#include <chrono>

#include "http3/include/if_client.h"
#include "http3/include/if_request.h"
#include "http3/include/if_response.h"

using namespace quicx;

class InteropClient {
private:
    std::unique_ptr<IClient> client_;
    std::string download_dir_;
    int completed_;
    int total_;
    FILE* keylog_file_;

public:
    InteropClient(const std::string& download_dir)
        : download_dir_(download_dir), completed_(0), total_(0), keylog_file_(nullptr) {}

    ~InteropClient() {
        if (keylog_file_) {
            fclose(keylog_file_);
        }
    }

    bool Init(const char* keylog_path = nullptr) {
        // Open SSLKEYLOG file if specified
        if (keylog_path) {
            keylog_file_ = fopen(keylog_path, "a");
            if (keylog_file_) {
                std::cout << "SSLKEYLOG enabled: " << keylog_path << std::endl;
            } else {
                std::cerr << "Warning: Failed to open SSLKEYLOG file: " << keylog_path << std::endl;
            }
        }

        client_ = IClient::Create();

        Http3Config config;
        config.thread_num_ = 4;
        config.log_level_ = LogLevel::kInfo;

        // Enable ECN if requested
        const char* enable_ecn = std::getenv("ENABLE_ECN");
        if (enable_ecn && std::atoi(enable_ecn) == 1) {
            config.enable_ecn_ = true;
            std::cout << "ECN enabled" << std::endl;
        }

        if (!client_->Init(config)) {
            std::cerr << "Failed to initialize client" << std::endl;
            return false;
        }

        std::cout << "Client initialized" << std::endl;
        std::cout << "Download directory: " << download_dir_ << std::endl;

        return true;
    }

    std::string ExtractFilename(const std::string& url) {
        // Extract filename from URL (e.g., https://server:port/file.bin -> file.bin)
        size_t last_slash = url.find_last_of('/');
        if (last_slash != std::string::npos && last_slash + 1 < url.length()) {
            return url.substr(last_slash + 1);
        }
        return "download.bin";
    }

    bool SaveToFile(const std::string& filename, const std::string& data) {
        std::string filepath = download_dir_ + "/" + filename;

        std::ofstream file(filepath, std::ios::binary);
        if (!file.is_open()) {
            std::cerr << "Failed to open file for writing: " << filepath << std::endl;
            return false;
        }

        file.write(data.data(), data.size());
        if (!file) {
            std::cerr << "Failed to write data to file: " << filepath << std::endl;
            return false;
        }

        file.close();
        std::cout << "Saved to: " << filepath << " (" << data.size() << " bytes)" << std::endl;
        return true;
    }

    bool DownloadFile(const std::string& url) {
        auto request = IRequest::Create();

        std::cout << "Downloading: " << url << std::endl;

        bool success = false;
        bool done = false;

        client_->DoRequest(url, HttpMethod::kGet, request,
            [this, &success, &done, url](std::shared_ptr<IResponse> response, uint32_t error) {
                if (error != 0) {
                    std::cerr << "Request failed with error: " << error << std::endl;
                    success = false;
                } else {
                    std::cout << "Response status: " << response->GetStatusCode() << std::endl;

                    auto body = response->GetBody();
                    if (!body) {
                        std::cerr << "Response has no body" << std::endl;
                        success = false;
                    } else {
                        // Read body data in a loop until EOF
                        std::vector<uint8_t> data;
                        const size_t CHUNK_SIZE = 64 * 1024;  // 64KB chunks

                        while (true) {
                            std::vector<uint8_t> chunk(CHUNK_SIZE);
                            uint32_t read_len = body->Read(chunk.data(), CHUNK_SIZE);

                            if (read_len == 0) {
                                break;  // EOF
                            }

                            data.insert(data.end(), chunk.begin(), chunk.begin() + read_len);
                        }

                        std::cout << "Response body length: " << data.size() << " bytes" << std::endl;

                        // Extract filename and save
                        std::string filename = ExtractFilename(url);
                        std::string str_data(reinterpret_cast<char*>(data.data()), data.size());
                        success = SaveToFile(filename, str_data);
                    }
                }
                completed_++;
                done = true;
            });

        // Wait for this specific request to complete
        while (!done) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        return success;
    }

    bool DownloadAll(const std::vector<std::string>& urls) {
        total_ = urls.size();
        completed_ = 0;

        for (const auto& url : urls) {
            if (!DownloadFile(url)) {
                std::cerr << "Failed to download: " << url << std::endl;
                return false;
            }
        }

        // Wait for all downloads to complete
        while (completed_ < total_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        std::cout << "All downloads completed: " << completed_ << "/" << total_ << std::endl;
        return true;
    }
};

std::vector<std::string> ParseUrls(const std::string& requests) {
    std::vector<std::string> urls;
    std::istringstream iss(requests);
    std::string url;

    while (iss >> url) {
        urls.push_back(url);
    }

    return urls;
}

int main(int argc, char* argv[]) {
    // Read configuration from environment
    const char* server_env = std::getenv("SERVER");
    const char* port_env = std::getenv("PORT");
    const char* requests_env = std::getenv("REQUESTS");
    const char* downloads_env = std::getenv("DOWNLOADS");
    const char* qlog_env = std::getenv("QLOGDIR");
    const char* keylog_env = std::getenv("SSLKEYLOGFILE");

    if (!server_env || !requests_env) {
        std::cerr << "Error: SERVER and REQUESTS environment variables are required" << std::endl;
        return 1;
    }

    std::string server = server_env;
    uint16_t port = port_env ? std::atoi(port_env) : 443;
    std::string downloads_dir = downloads_env ? downloads_env : "/downloads";

    std::vector<std::string> urls = ParseUrls(requests_env);

    std::cout << "========================================" << std::endl;
    std::cout << "quicX QUIC Interop Test Client" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Server: " << server << ":" << port << std::endl;
    std::cout << "Downloads: " << downloads_dir << std::endl;
    std::cout << "URLs to download: " << urls.size() << std::endl;
    for (const auto& url : urls) {
        std::cout << "  - " << url << std::endl;
    }
    if (qlog_env) {
        std::cout << "QLOG: " << qlog_env << std::endl;
    }
    if (keylog_env) {
        std::cout << "KEYLOG: " << keylog_env << std::endl;
    }
    std::cout << "========================================" << std::endl;
    std::cout << std::endl;

    InteropClient client(downloads_dir);

    if (!client.Init(keylog_env)) {
        return 1;
    }

    if (!client.DownloadAll(urls)) {
        return 1;
    }

    std::cout << "Client finished successfully" << std::endl;
    return 0;
}
