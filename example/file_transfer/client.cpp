#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>

#include "http3/include/if_async_handler.h"
#include "http3/include/if_client.h"
#include "http3/include/if_request.h"
#include "http3/include/if_response.h"

namespace fs = std::filesystem;

class ProgressBar {
private:
    uint64_t total_bytes_;
    std::atomic<uint64_t> transferred_bytes_;
    std::chrono::steady_clock::time_point start_time_;
    std::atomic<bool> running_;
    std::thread update_thread_;
    std::string operation_;

public:
    ProgressBar(uint64_t total_bytes, uint64_t initial_bytes = 0, const std::string& op = "Progress"):
        total_bytes_(total_bytes),
        transferred_bytes_(initial_bytes),
        running_(true),
        operation_(op) {
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

    void Update(uint64_t bytes) { transferred_bytes_ = bytes; }
    void Add(uint64_t bytes) { transferred_bytes_ += bytes; }

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

        uint64_t transferred = transferred_bytes_.load();
        double progress = total_bytes_ > 0 ? static_cast<double>(transferred) / total_bytes_ : 0.0;

        // Calculate speed (MB/s)
        double elapsed_sec = elapsed.count() / 1000.0;
        double speed_mbps = elapsed_sec > 0 ? (transferred / 1024.0 / 1024.0) / elapsed_sec : 0.0;

        // Calculate ETA
        uint64_t remaining = total_bytes_ > transferred ? total_bytes_ - transferred : 0;
        double eta_sec = speed_mbps > 0 ? (remaining / 1024.0 / 1024.0) / speed_mbps : 0.0;

        // Progress bar
        const int bar_width = 40;
        int filled = static_cast<int>(progress * bar_width);

        std::cout << "\r" << operation_ << ": [";
        for (int i = 0; i < bar_width; ++i) {
            if (i < filled)
                std::cout << "=";
            else if (i == filled)
                std::cout << ">";
            else
                std::cout << " ";
        }
        std::cout << "] " << std::fixed << std::setprecision(1) << (progress * 100.0) << "% | ";

        // Transferred / Total
        std::cout << FormatBytes(transferred) << "/" << FormatBytes(total_bytes_) << " | ";

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

// Streaming download handler
class FileDownloadHandler : public quicx::IAsyncClientHandler {
private:
    std::string output_file_;
    std::ofstream file_;
    uint64_t total_size_ = 0;
    uint64_t resume_from_ = 0;
    uint64_t bytes_received_ = 0;
    std::string checksum_;
    std::unique_ptr<ProgressBar> progress_;
    std::chrono::steady_clock::time_point start_time_;

    std::mutex mutex_;
    std::condition_variable cv_;
    bool completed_ = false;
    bool success_ = false;

public:
    FileDownloadHandler(const std::string& output_file, uint64_t resume_from = 0):
        output_file_(output_file),
        resume_from_(resume_from) {}

    void OnHeaders(std::shared_ptr<quicx::IResponse> response) override {
        int status = response->GetStatusCode();

        if (status != 200 && status != 206) {
            std::cerr << "\nError: HTTP " << status << std::endl;
            Complete(false);
            return;
        }

        // Get total size
        std::string content_length;
        response->GetHeader("Content-Length", content_length);
        if (!content_length.empty()) {
            total_size_ = std::stoull(content_length);

            // If resuming, add the already downloaded bytes
            if (status == 206) {
                total_size_ += resume_from_;
            }
        }

        // Get checksum if available
        response->GetHeader("X-Checksum", checksum_);

        std::cout << "File size: " << total_size_ << " bytes (" << std::fixed << std::setprecision(2)
                  << (total_size_ / 1024.0 / 1024.0) << " MB)" << std::endl;

        // Open output file
        std::ios_base::openmode mode = std::ios::binary;
        if (resume_from_ > 0) {
            mode |= std::ios::app;  // Append mode
        } else {
            mode |= std::ios::trunc;  // Truncate mode
        }

        file_.open(output_file_, mode);
        if (!file_) {
            std::cerr << "\nError: Cannot open output file: " << output_file_ << std::endl;
            Complete(false);
            return;
        }

        // Start progress bar
        start_time_ = std::chrono::steady_clock::now();
        progress_ = std::make_unique<ProgressBar>(total_size_, resume_from_, "Download");
        bytes_received_ = resume_from_;
    }

    void OnBodyChunk(const uint8_t* data, size_t length, bool is_last) override {
        if (file_.is_open() && length > 0) {
            file_.write(reinterpret_cast<const char*>(data), length);
            bytes_received_ += length;

            if (progress_) {
                progress_->Update(bytes_received_);
            }
        }

        if (is_last) {
            if (progress_) {
                progress_->Stop();
            }

            if (file_.is_open()) {
                file_.close();
            }

            auto end_time = std::chrono::steady_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time_);

            std::cout << "Download completed in " << std::fixed << std::setprecision(2) << (duration.count() / 1000.0)
                      << "s";

            if (resume_from_ > 0) {
                std::cout << " (resumed from " << std::fixed << std::setprecision(2)
                          << (resume_from_ / 1024.0 / 1024.0) << " MB)";
            }
            std::cout << std::endl;

            // Calculate average speed
            double avg_speed = (bytes_received_ / 1024.0 / 1024.0) / (duration.count() / 1000.0);
            std::cout << "Average speed: " << std::fixed << std::setprecision(1) << avg_speed << " MB/s" << std::endl;

            // Verify checksum if available
            if (!checksum_.empty()) {
                std::cout << "Verifying checksum... ";
                std::string file_checksum = CalculateChecksum(output_file_);
                if (file_checksum == checksum_) {
                    std::cout << "OK" << std::endl;
                } else {
                    std::cout << "MISMATCH!" << std::endl;
                    std::cout << "  Expected: " << checksum_ << std::endl;
                    std::cout << "  Got:      " << file_checksum << std::endl;
                }
            }

            Complete(true);
        }
    }

    void OnError(uint32_t error_code) override {
        std::cerr << "\nDownload error: protocol/network error code " << error_code << std::endl;
        if (progress_) {
            progress_->Stop();
        }
        if (file_.is_open()) {
            file_.close();
        }
        Complete(false);
    }

    bool WaitForCompletion(int timeout_sec = 120) {
        std::unique_lock<std::mutex> lock(mutex_);
        return cv_.wait_for(lock, std::chrono::seconds(timeout_sec), [this] { return completed_; }) && success_;
    }

private:
    void Complete(bool success) {
        std::lock_guard<std::mutex> lock(mutex_);
        completed_ = true;
        success_ = success;
        cv_.notify_all();
    }

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

class FileTransferClient {
private:
    std::shared_ptr<quicx::IClient> client_;

public:
    FileTransferClient() { client_ = quicx::IClient::Create(); }

    bool Init() {
        quicx::Http3ClientConfig config;
        config.quic_config_.verify_peer_ = false;  // examples use self-signed certs
        config.quic_config_.config_.worker_thread_num_ = 4;
        config.quic_config_.config_.log_level_ = quicx::LogLevel::kDebug;
        config.connection_timeout_ms_ = 30000;  // 30s timeout

        if (!client_->Init(config)) {
            std::cerr << "Failed to initialize client" << std::endl;
            return false;
        }

        return true;
    }

    bool Download(const std::string& url, const std::string& output_file) {
        std::cout << "Downloading: " << url << std::endl;

        // Check if file exists (for resume)
        uint64_t resume_from = 0;
        if (fs::exists(output_file)) {
            resume_from = fs::file_size(output_file);
            if (resume_from > 0) {
                std::cout << "Resuming download from " << resume_from << " bytes (" << std::fixed
                          << std::setprecision(2) << (resume_from / 1024.0 / 1024.0) << " MB)" << std::endl;
            }
        }

        // Create request
        auto request = quicx::IRequest::Create();

        // Add Range header if resuming
        if (resume_from > 0) {
            request->AddHeader("Range", "bytes=" + std::to_string(resume_from) + "-");
        }

        // Create streaming handler
        auto handler = std::make_shared<FileDownloadHandler>(output_file, resume_from);

        // Send request with streaming handler
        if (!client_->DoRequest(url, quicx::HttpMethod::kGet, request, handler)) {
            std::cerr << "Failed to send request" << std::endl;
            return false;
        }

        // Wait for completion
        return handler->WaitForCompletion();
    }

    bool Upload(const std::string& url, const std::string& input_file) {
        std::cout << "Uploading: " << input_file << std::endl;

        // Check if file exists
        if (!fs::exists(input_file)) {
            std::cerr << "Error: File not found: " << input_file << std::endl;
            return false;
        }

        uint64_t file_size = fs::file_size(input_file);
        std::cout << "File size: " << file_size << " bytes (" << std::fixed << std::setprecision(2)
                  << (file_size / 1024.0 / 1024.0) << " MB)" << std::endl;

        // Create request
        auto request = quicx::IRequest::Create();
        request->AddHeader("Content-Length", std::to_string(file_size));
        request->AddHeader("Content-Type", "application/octet-stream");

        // Open file for streaming
        auto file = std::make_shared<std::ifstream>(input_file, std::ios::binary);
        if (!file->is_open()) {
            std::cerr << "Error: Cannot open file: " << input_file << std::endl;
            return false;
        }

        // Create progress bar
        auto progress = std::make_shared<ProgressBar>(file_size, 0, "Upload");
        auto bytes_sent = std::make_shared<std::atomic<uint64_t>>(0);

        // Set request body provider for streaming upload
        // Note: Progress bar shows data submitted to HTTP/3 layer, not actual network transfer
        // The actual network transfer may take longer due to QUIC buffering and congestion control
        request->SetRequestBodyProvider([file, progress, bytes_sent, file_size](uint8_t* buf, size_t size) -> size_t {
            if (!file->is_open() || file->eof()) {
                return 0;
            }

            file->read(reinterpret_cast<char*>(buf), size);
            size_t actually_read = file->gcount();

            *bytes_sent += actually_read;
            progress->Update(bytes_sent->load());

            if (file->eof() || bytes_sent->load() >= file_size) {
                file->close();
                // Don't stop progress bar here - wait for server response
                // The actual network transfer is still in progress
            }

            return actually_read;
        });

        // Send request
        std::mutex mutex;
        std::condition_variable cv;
        bool completed = false;
        bool success = false;

        auto start_time = std::chrono::steady_clock::now();

        client_->DoRequest(url, quicx::HttpMethod::kPost, request,
            [&, progress](std::shared_ptr<quicx::IResponse> response, uint32_t error) {
                // Stop progress bar when we receive the response (transfer complete)
                if (progress) {
                    progress->Stop();
                }
                
                if (error != 0) {
                    std::cerr << "\nError: Upload failed with error code " << error << std::endl;
                } else {
                    int status = response->GetStatusCode();
                    if (status == 200) {
                        // Use actual response time - this is when the server confirmed receipt
                        auto end_time = std::chrono::steady_clock::now();
                        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

                        std::cout << "Upload completed in " << std::fixed << std::setprecision(2)
                                  << (duration.count() / 1000.0) << "s" << std::endl;

                        // Calculate average speed based on actual transfer time
                        double avg_speed = (file_size / 1024.0 / 1024.0) / (duration.count() / 1000.0);
                        std::cout << "Average speed: " << std::fixed << std::setprecision(1) << avg_speed << " MB/s"
                                  << std::endl;

                        // Check server checksum
                        std::string server_checksum;
                        response->GetHeader("X-Checksum", server_checksum);
                        if (!server_checksum.empty()) {
                            std::cout << "Server checksum: " << server_checksum << std::endl;
                        }

                        success = true;
                    } else {
                        std::cerr << "\nError: HTTP " << status << std::endl;
                        std::cerr << "Response: " << response->GetBodyAsString() << std::endl;
                    }
                }

                std::lock_guard<std::mutex> lock(mutex);
                completed = true;
                cv.notify_all();
            });

        // Wait for completion
        std::unique_lock<std::mutex> lock(mutex);
        cv.wait_for(lock, std::chrono::seconds(120), [&completed] { return completed; });

        return success;
    }
};

void PrintUsage(const char* program) {
    std::cout << "Usage:" << std::endl;
    std::cout << "  Download: " << program << " download <url> [output_file]" << std::endl;
    std::cout << "  Upload:   " << program << " upload <url> <input_file>" << std::endl;
    std::cout << std::endl;
    std::cout << "Examples:" << std::endl;
    std::cout << "  " << program << " download https://localhost:7006/file.bin" << std::endl;
    std::cout << "  " << program << " download https://localhost:7006/file.bin ./downloads/myfile.bin" << std::endl;
    std::cout << "  " << program << " upload https://localhost:7006/upload/file.bin ./myfile.bin" << std::endl;
}

int main(int argc, char* argv[]) {
    if (argc < 3) {
        PrintUsage(argv[0]);
        return 1;
    }

    std::string command = argv[1];
    std::string url = argv[2];

    FileTransferClient client;

    if (!client.Init()) {
        return 1;
    }

    if (command == "download") {
        std::string output_file;

        if (argc > 3) {
            output_file = argv[3];
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

        if (!client.Download(url, output_file)) {
            std::cerr << "Download failed" << std::endl;
            return 1;
        }

        std::cout << "File saved to: " << output_file << std::endl;

    } else if (command == "upload") {
        if (argc < 4) {
            std::cerr << "Error: Missing input file" << std::endl;
            PrintUsage(argv[0]);
            return 1;
        }

        std::string input_file = argv[3];

        if (!client.Upload(url, input_file)) {
            std::cerr << "Upload failed" << std::endl;
            return 1;
        }

    } else {
        std::cerr << "Unknown command: " << command << std::endl;
        PrintUsage(argv[0]);
        return 1;
    }

    return 0;
}
