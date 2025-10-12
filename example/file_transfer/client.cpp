#include <thread>
#include <chrono>
#include <iostream>
#include <fstream>
#include <sstream>
#include <atomic>
#include <ctime>
#include "http3/include/if_client.h"

std::atomic<int> pending_requests(0);

void WaitForCompletion() {
    while (pending_requests.load() > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

std::string FormatFileSize(size_t bytes) {
    const char* units[] = {"B", "KB", "MB", "GB"};
    int unit_index = 0;
    double size = static_cast<double>(bytes);
    
    while (size >= 1024.0 && unit_index < 3) {
        size /= 1024.0;
        unit_index++;
    }
    
    char buf[64];
    snprintf(buf, sizeof(buf), "%.2f %s", size, units[unit_index]);
    return std::string(buf);
}

std::string GenerateMultipartBoundary() {
    std::ostringstream oss;
    oss << "----QuicXBoundary" << time(nullptr);
    return oss.str();
}

std::string CreateMultipartBody(const std::string& filename, const std::string& content, const std::string& boundary) {
    std::ostringstream body;
    
    body << "--" << boundary << "\r\n";
    body << "Content-Disposition: form-data; name=\"file\"; filename=\"" << filename << "\"\r\n";
    body << "Content-Type: application/octet-stream\r\n";
    body << "\r\n";
    body << content;
    body << "\r\n";
    body << "--" << boundary << "--\r\n";
    
    return body.str();
}

bool ReadLocalFile(const std::string& filepath, std::string& content) {
    std::ifstream file(filepath, std::ios::binary);
    if (!file) {
        std::cerr << "Error: Cannot open file: " << filepath << std::endl;
        return false;
    }
    
    // Get file size
    file.seekg(0, std::ios::end);
    size_t size = file.tellg();
    file.seekg(0, std::ios::beg);
    
    // Read content
    content.resize(size);
    file.read(&content[0], size);
    file.close();
    
    return true;
}

bool WriteLocalFile(const std::string& filepath, const std::string& content) {
    std::ofstream file(filepath, std::ios::binary);
    if (!file) {
        std::cerr << "Error: Cannot write file: " << filepath << std::endl;
        return false;
    }
    
    file.write(content.data(), content.size());
    file.close();
    
    return true;
}

std::string ExtractFilename(const std::string& path) {
    size_t last_slash = path.find_last_of("/\\");
    if (last_slash == std::string::npos) {
        return path;
    }
    return path.substr(last_slash + 1);
}

void UploadFile(quicx::http3::IClient* client, const std::string& filepath) {
    std::string content;
    if (!ReadLocalFile(filepath, content)) {
        return;
    }
    
    std::string filename = ExtractFilename(filepath);
    std::string boundary = GenerateMultipartBoundary();
    std::string body = CreateMultipartBody(filename, content, boundary);
    
    std::cout << "Uploading: " << filename << " (" << FormatFileSize(content.size()) << ")" << std::endl;
    
    pending_requests++;
    auto request = quicx::http3::IRequest::Create();
    request->AddHeader("Content-Type", "multipart/form-data; boundary=" + boundary);
    request->SetBody(body);
    
    client->DoRequest(
        "https://127.0.0.1:8884/upload",
        quicx::http3::HttpMethod::kPost,
        request,
        [filename](std::shared_ptr<quicx::http3::IResponse> response, uint32_t error) {
            if (error == 0) {
                std::cout << "  Status: " << response->GetStatusCode() << std::endl;
                std::cout << "  Response: " << response->GetBody() << std::endl;
            } else {
                std::cout << "  Error: " << error << std::endl;
            }
            std::cout << std::endl;
            pending_requests--;
        }
    );
}

void DownloadFile(quicx::http3::IClient* client, const std::string& filename, const std::string& save_as) {
    std::cout << "Downloading: " << filename << std::endl;
    
    pending_requests++;
    auto request = quicx::http3::IRequest::Create();
    
    client->DoRequest(
        "https://127.0.0.1:8884/files/" + filename,
        quicx::http3::HttpMethod::kGet,
        request,
        [filename, save_as](std::shared_ptr<quicx::http3::IResponse> response, uint32_t error) {
            if (error == 0) {
                std::cout << "  Status: " << response->GetStatusCode() << std::endl;
                
                if (response->GetStatusCode() == 200) {
                    const std::string& content = response->GetBody();
                    std::string output_file = save_as.empty() ? ("downloaded_" + filename) : save_as;
                    
                    if (WriteLocalFile(output_file, content)) {
                        std::cout << "  Saved to: " << output_file << " (" 
                                  << FormatFileSize(content.size()) << ")" << std::endl;
                    } else {
                        std::cout << "  Failed to save file" << std::endl;
                    }
                } else {
                    std::cout << "  Response: " << response->GetBody() << std::endl;
                }
            } else {
                std::cout << "  Error: " << error << std::endl;
            }
            std::cout << std::endl;
            pending_requests--;
        }
    );
}

void ListFiles(quicx::http3::IClient* client) {
    std::cout << "Listing files..." << std::endl;
    
    pending_requests++;
    auto request = quicx::http3::IRequest::Create();
    
    client->DoRequest(
        "https://127.0.0.1:8884/files",
        quicx::http3::HttpMethod::kGet,
        request,
        [](std::shared_ptr<quicx::http3::IResponse> response, uint32_t error) {
            if (error == 0) {
                std::cout << "  Status: " << response->GetStatusCode() << std::endl;
                std::cout << "  Files: " << response->GetBody() << std::endl;
            } else {
                std::cout << "  Error: " << error << std::endl;
            }
            std::cout << std::endl;
            pending_requests--;
        }
    );
}

void DeleteFile(quicx::http3::IClient* client, const std::string& filename) {
    std::cout << "Deleting: " << filename << std::endl;
    
    pending_requests++;
    auto request = quicx::http3::IRequest::Create();
    
    client->DoRequest(
        "https://127.0.0.1:8884/files/" + filename,
        quicx::http3::HttpMethod::kDelete,
        request,
        [filename](std::shared_ptr<quicx::http3::IResponse> response, uint32_t error) {
            if (error == 0) {
                std::cout << "  Status: " << response->GetStatusCode() << std::endl;
                if (response->GetStatusCode() != 204) {
                    std::cout << "  Response: " << response->GetBody() << std::endl;
                } else {
                    std::cout << "  Successfully deleted" << std::endl;
                }
            } else {
                std::cout << "  Error: " << error << std::endl;
            }
            std::cout << std::endl;
            pending_requests--;
        }
    );
}

void GetStats(quicx::http3::IClient* client) {
    std::cout << "Getting server statistics..." << std::endl;
    
    pending_requests++;
    auto request = quicx::http3::IRequest::Create();
    
    client->DoRequest(
        "https://127.0.0.1:8884/stats",
        quicx::http3::HttpMethod::kGet,
        request,
        [](std::shared_ptr<quicx::http3::IResponse> response, uint32_t error) {
            if (error == 0) {
                std::cout << "  Status: " << response->GetStatusCode() << std::endl;
                std::cout << "  Stats: " << response->GetBody() << std::endl;
            } else {
                std::cout << "  Error: " << error << std::endl;
            }
            std::cout << std::endl;
            pending_requests--;
        }
    );
}

// Create a test file for demonstration
void CreateTestFile(const std::string& filename, size_t size_kb) {
    std::ofstream file(filename, std::ios::binary);
    if (!file) {
        std::cerr << "Cannot create test file: " << filename << std::endl;
        return;
    }
    
    // Create content with repeating pattern
    std::string pattern = "QuicX HTTP/3 File Transfer Test - ";
    size_t bytes_written = 0;
    size_t target_bytes = size_kb * 1024;
    
    while (bytes_written < target_bytes) {
        size_t to_write = std::min(pattern.size(), target_bytes - bytes_written);
        file.write(pattern.data(), to_write);
        bytes_written += to_write;
    }
    
    file.close();
    std::cout << "Created test file: " << filename << " (" << FormatFileSize(target_bytes) << ")" << std::endl;
}

int main(int argc, char* argv[]) {
    auto client = quicx::http3::IClient::Create();

    quicx::http3::Http3Config config;
    config.thread_num_ = 2;
    config.log_level_ = quicx::http3::LogLevel::kError;
    client->Init(config);

    std::cout << "==================================" << std::endl;
    std::cout << "File Transfer Client Demo" << std::endl;
    std::cout << "==================================" << std::endl;
    std::cout << std::endl;

    // If command line arguments provided, use them
    if (argc >= 3) {
        std::string command = argv[1];
        
        if (command == "upload") {
            UploadFile(client.get(), argv[2]);
            WaitForCompletion();
        } else if (command == "download" && argc >= 3) {
            std::string save_as = argc >= 4 ? argv[3] : "";
            DownloadFile(client.get(), argv[2], save_as);
            WaitForCompletion();
        } else if (command == "list") {
            ListFiles(client.get());
            WaitForCompletion();
        } else if (command == "delete") {
            DeleteFile(client.get(), argv[2]);
            WaitForCompletion();
        } else if (command == "stats") {
            GetStats(client.get());
            WaitForCompletion();
        } else {
            std::cout << "Usage:" << std::endl;
            std::cout << "  " << argv[0] << " upload <filepath>" << std::endl;
            std::cout << "  " << argv[0] << " download <filename> [save_as]" << std::endl;
            std::cout << "  " << argv[0] << " list" << std::endl;
            std::cout << "  " << argv[0] << " delete <filename>" << std::endl;
            std::cout << "  " << argv[0] << " stats" << std::endl;
        }
        return 0;
    }

    // Demo mode - run automated tests
    std::cout << "Running automated demo..." << std::endl;
    std::cout << std::endl;

    // Create test files
    std::cout << "Step 1: Creating test files" << std::endl;
    std::cout << "----------------------------------" << std::endl;
    CreateTestFile("test_small.txt", 10);    // 10 KB
    CreateTestFile("test_medium.txt", 100);  // 100 KB
    CreateTestFile("test_large.txt", 1024);  // 1 MB
    std::cout << std::endl;
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    // Test 1: Upload small file
    std::cout << "Step 2: Upload small file" << std::endl;
    std::cout << "----------------------------------" << std::endl;
    UploadFile(client.get(), "test_small.txt");
    WaitForCompletion();

    // Test 2: Upload medium file
    std::cout << "Step 3: Upload medium file" << std::endl;
    std::cout << "----------------------------------" << std::endl;
    UploadFile(client.get(), "test_medium.txt");
    WaitForCompletion();

    // Test 3: Upload large file
    std::cout << "Step 4: Upload large file" << std::endl;
    std::cout << "----------------------------------" << std::endl;
    UploadFile(client.get(), "test_large.txt");
    WaitForCompletion();

    // Test 4: List all files
    std::cout << "Step 5: List all files" << std::endl;
    std::cout << "----------------------------------" << std::endl;
    ListFiles(client.get());
    WaitForCompletion();

    // Test 5: Get statistics
    std::cout << "Step 6: Get server statistics" << std::endl;
    std::cout << "----------------------------------" << std::endl;
    GetStats(client.get());
    WaitForCompletion();

    // Test 6: Download a file
    std::cout << "Step 7: Download file" << std::endl;
    std::cout << "----------------------------------" << std::endl;
    DownloadFile(client.get(), "test_medium.txt", "downloaded_test.txt");
    WaitForCompletion();

    // Test 7: Delete a file
    std::cout << "Step 8: Delete file" << std::endl;
    std::cout << "----------------------------------" << std::endl;
    DeleteFile(client.get(), "test_small.txt");
    WaitForCompletion();

    // Test 8: List files again
    std::cout << "Step 9: List files again" << std::endl;
    std::cout << "----------------------------------" << std::endl;
    ListFiles(client.get());
    WaitForCompletion();

    // Test 9: Get statistics again
    std::cout << "Step 10: Get final statistics" << std::endl;
    std::cout << "----------------------------------" << std::endl;
    GetStats(client.get());
    WaitForCompletion();

    std::cout << "==================================" << std::endl;
    std::cout << "All tests completed!" << std::endl;
    std::cout << "==================================" << std::endl;

    return 0;
}

