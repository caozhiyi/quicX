#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

#include "http3/include/if_client.h"
#include "http3/include/if_request.h"
#include "http3/include/if_response.h"

namespace fs = std::filesystem;

class ProgressBar {
private:
    uint64_t total_bytes_;
    std::atomic<uint64_t> downloaded_bytes_;
    std::chrono::steady_clock::time_point start_time_;
    std::atomic<bool> running_;
    std::thread update_thread_;

public:
    ProgressBar(uint64_t total_bytes, uint64_t initial_bytes = 0):
        total_bytes_(total_bytes),
        downloaded_bytes_(initial_bytes),
        running_(true) {
        start_time_ = std::chrono::steady_clock::now();

        // Start update thread
        update_thread_ = std::thread([this]() {
            while (running_) {
                Display();
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        });
    }

    ~ProgressBar() { Stop(); }

    void Update(uint64_t bytes) { downloaded_bytes_ = bytes; }

    void Stop() {
        if (running_) {
            running_ = false;
            if (update_thread_.joinable()) {
                update_thread_.join();
            }
            Display();  // Final display
            std::cout << std::endl;
        }
    }

private:
    void Display() {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time_);

        uint64_t downloaded = downloaded_bytes_.load();
        double progress = total_bytes_ > 0 ? static_cast<double>(downloaded) / total_bytes_ : 0.0;

        // Calculate speed (MB/s)
        double elapsed_sec = elapsed.count() / 1000.0;
        double speed_mbps = elapsed_sec > 0 ? (downloaded / 1024.0 / 1024.0) / elapsed_sec : 0.0;

        // Calculate ETA
        uint64_t remaining = total_bytes_ - downloaded;
        double eta_sec = speed_mbps > 0 ? (remaining / 1024.0 / 1024.0) / speed_mbps : 0.0;

        // Progress bar
        const int bar_width = 40;
        int filled = static_cast<int>(progress * bar_width);

        std::cout << "\rProgress: [";
        for (int i = 0; i < bar_width; ++i) {
            if (i < filled)
                std::cout << "=";
            else if (i == filled)
                std::cout << ">";
            else
                std::cout << " ";
        }
        std::cout << "] " << std::fixed << std::setprecision(1) << (progress * 100.0) << "% | ";

        // Downloaded / Total
        std::cout << FormatBytes(downloaded) << "/" << FormatBytes(total_bytes_) << " | ";

        // Speed
        std::cout << std::fixed << std::setprecision(1) << speed_mbps << " MB/s | ";

        // ETA
        std::cout << "ETA: " << FormatTime(static_cast<int>(eta_sec));

        std::cout << std::flush;
    }

    std::string FormatBytes(uint64_t bytes) {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(2);

        if (bytes >= 1024 * 1024 * 1024) {
            oss << (bytes / 1024.0 / 1024.0 / 1024.0) << " GB";
        } else if (bytes >= 1024 * 1024) {
            oss << (bytes / 1024.0 / 1024.0) << " MB";
        } else if (bytes >= 1024) {
            oss << (bytes / 1024.0) << " KB";
        } else {
            oss << bytes << " B";
        }

        return oss.str();
    }

    std::string FormatTime(int seconds) {
        if (seconds < 60) {
            return std::to_string(seconds) + "s";
        } else if (seconds < 3600) {
            return std::to_string(seconds / 60) + "m " + std::to_string(seconds % 60) + "s";
        } else {
            int hours = seconds / 3600;
            int mins = (seconds % 3600) / 60;
            return std::to_string(hours) + "h " + std::to_string(mins) + "m";
        }
    }
};

class FileDownloader {
private:
    std::shared_ptr<quicx::IClient> client_;
    std::string url_;
    std::string output_file_;
    uint64_t resume_from_;

public:
    FileDownloader(const std::string& url, const std::string& output_file):
        url_(url),
        output_file_(output_file),
        resume_from_(0) {
        client_ = quicx::IClient::Create();
    }

    bool Init() {
        quicx::Http3Config config;
        config.thread_num_ = 4;
        config.log_level_ = quicx::LogLevel::kWarn;  // Reduce noise
        config.connection_timeout_ms_ = 30000;       // 30s timeout

        if (!client_->Init(config)) {
            std::cerr << "Failed to initialize client" << std::endl;
            return false;
        }

        return true;
    }

    bool Download() {
        std::cout << "Downloading: " << url_ << std::endl;

        // Check if file exists (for resume)
        if (fs::exists(output_file_)) {
            resume_from_ = fs::file_size(output_file_);
            if (resume_from_ > 0) {
                std::cout << "Resuming download from " << resume_from_ << " bytes (" << std::fixed
                          << std::setprecision(2) << (resume_from_ / 1024.0 / 1024.0) << " MB)" << std::endl;
            }
        }

        // Create request
        auto request = quicx::IRequest::Create();

        // Add Range header if resuming
        if (resume_from_ > 0) {
            request->AddHeader("Range", "bytes=" + std::to_string(resume_from_) + "-");
        }

        // Send request
        bool success = false;
        uint64_t total_size = 0;
        std::string checksum;

        auto start_time = std::chrono::steady_clock::now();

        client_->DoRequest(
            url_, quicx::HttpMethod::kGet, request, [&](std::shared_ptr<quicx::IResponse> response, uint32_t error) {
                if (error != 0) {
                    std::cerr << "\nError: Request failed with error code " << error << std::endl;
                    return;
                }

                int status = response->GetStatusCode();

                if (status != 200 && status != 206) {
                    std::cerr << "\nError: HTTP " << status << std::endl;
                    return;
                }

                // Get total size
                std::string content_length;
                response->GetHeader("Content-Length", content_length);
                if (!content_length.empty()) {
                    total_size = std::stoull(content_length);

                    // If resuming, add the already downloaded bytes
                    if (status == 206) {
                        total_size += resume_from_;
                    }
                }

                // Get checksum if available
                response->GetHeader("X-Checksum", checksum);

                std::cout << "File size: " << total_size << " bytes (" << std::fixed << std::setprecision(2)
                          << (total_size / 1024.0 / 1024.0) << " MB)" << std::endl;

                // Get response body
                std::string body = response->GetBodyAsString();

                // Save to file
                std::ios_base::openmode mode = std::ios::binary;
                if (resume_from_ > 0) {
                    mode |= std::ios::app;  // Append mode
                } else {
                    mode |= std::ios::trunc;  // Truncate mode
                }

                std::ofstream outfile(output_file_, mode);
                if (!outfile) {
                    std::cerr << "\nError: Cannot open output file: " << output_file_ << std::endl;
                    return;
                }

                // Show progress
                ProgressBar progress(total_size, resume_from_);

                // Write data
                outfile.write(body.data(), body.size());
                progress.Update(resume_from_ + body.size());

                outfile.close();
                progress.Stop();

                auto end_time = std::chrono::steady_clock::now();
                auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

                std::cout << "Download completed in " << std::fixed << std::setprecision(2)
                          << (duration.count() / 1000.0) << "s";

                if (resume_from_ > 0) {
                    std::cout << " (resumed from " << std::fixed << std::setprecision(2)
                              << (resume_from_ / 1024.0 / 1024.0) << " MB)";
                }
                std::cout << std::endl;

                // Calculate average speed
                double avg_speed = (total_size / 1024.0 / 1024.0) / (duration.count() / 1000.0);
                std::cout << "Average speed: " << std::fixed << std::setprecision(1) << avg_speed << " MB/s"
                          << std::endl;

                // Verify checksum if available
                if (!checksum.empty()) {
                    std::cout << "Verifying checksum... ";
                    std::string file_checksum = CalculateChecksum(output_file_);
                    if (file_checksum == checksum) {
                        std::cout << "OK" << std::endl;
                    } else {
                        std::cout << "MISMATCH!" << std::endl;
                        std::cout << "  Expected: " << checksum << std::endl;
                        std::cout << "  Got:      " << file_checksum << std::endl;
                    }
                }

                success = true;
            });

        // Wait for completion
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        // Give some time for the request to complete
        for (int i = 0; i < 600 && !success; ++i) {  // Wait up to 60 seconds
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        return success;
    }

private:
    std::string CalculateChecksum(const std::string& filepath) {
        std::ifstream file(filepath, std::ios::binary);
        if (!file) {
            return "";
        }

        std::hash<std::string> hasher;
        std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        return std::to_string(hasher(content));
    }
};

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cout << "Usage: " << argv[0] << " <url> [output_file]" << std::endl;
        std::cout << "Example: " << argv[0] << " https://localhost:8443/file.bin" << std::endl;
        std::cout << "Example: " << argv[0] << " https://localhost:8443/file.bin ./downloads/myfile.bin" << std::endl;
        return 1;
    }

    std::string url = argv[1];
    std::string output_file;

    if (argc > 2) {
        output_file = argv[2];
    } else {
        // Extract filename from URL
        size_t last_slash = url.find_last_of('/');
        if (last_slash != std::string::npos) {
            output_file = url.substr(last_slash + 1);
        } else {
            output_file = "downloaded_file";
        }
    }

    // Create output directory if needed
    fs::path output_path(output_file);
    if (output_path.has_parent_path()) {
        fs::create_directories(output_path.parent_path());
    }

    FileDownloader downloader(url, output_file);

    if (!downloader.Init()) {
        return 1;
    }

    if (!downloader.Download()) {
        std::cerr << "Download failed" << std::endl;
        return 1;
    }

    std::cout << "File saved to: " << output_file << std::endl;

    return 0;
}
