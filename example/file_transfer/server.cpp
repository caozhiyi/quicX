#include <iostream>
#include <string>
#include <fstream>
#include <sstream>
#include <map>
#include <mutex>
#include <vector>
#include <ctime>
#include <sys/stat.h>
#include "http3/include/if_server.h"

// Storage directory for uploaded files
const std::string STORAGE_DIR = "./file_storage/";

// File metadata
struct FileInfo {
    std::string name;
    size_t size;
    std::string upload_time;
    std::string content_type;
};

class FileStorage {
private:
    std::map<std::string, FileInfo> files_;
    std::mutex mutex_;

    std::string GetCurrentTime() {
        time_t now = time(nullptr);
        char buf[80];
        strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", localtime(&now));
        return std::string(buf);
    }

public:
    FileStorage() {
        // Create storage directory if not exists
        mkdir(STORAGE_DIR.c_str(), 0755);
    }

    bool SaveFile(const std::string& filename, const std::string& content, const std::string& content_type) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        std::string filepath = STORAGE_DIR + filename;
        std::ofstream file(filepath, std::ios::binary);
        if (!file) {
            return false;
        }
        
        file.write(content.data(), content.size());
        file.close();

        FileInfo info;
        info.name = filename;
        info.size = content.size();
        info.upload_time = GetCurrentTime();
        info.content_type = content_type;
        files_[filename] = info;

        return true;
    }

    bool LoadFile(const std::string& filename, std::string& content, FileInfo& info) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        auto it = files_.find(filename);
        if (it == files_.end()) {
            // Try to read from disk anyway
            std::string filepath = STORAGE_DIR + filename;
            std::ifstream file(filepath, std::ios::binary);
            if (!file) {
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

            info.name = filename;
            info.size = size;
            info.upload_time = "Unknown";
            info.content_type = "application/octet-stream";
            
            return true;
        }

        info = it->second;
        std::string filepath = STORAGE_DIR + filename;
        std::ifstream file(filepath, std::ios::binary);
        if (!file) {
            return false;
        }

        content.assign((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        file.close();
        
        return true;
    }

    bool DeleteFile(const std::string& filename) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        std::string filepath = STORAGE_DIR + filename;
        if (remove(filepath.c_str()) != 0) {
            return false;
        }

        files_.erase(filename);
        return true;
    }

    std::vector<FileInfo> GetFileList() {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<FileInfo> result;
        for (const auto& pair : files_) {
            result.push_back(pair.second);
        }
        return result;
    }

    size_t GetTotalSize() {
        std::lock_guard<std::mutex> lock(mutex_);
        size_t total = 0;
        for (const auto& pair : files_) {
            total += pair.second.size;
        }
        return total;
    }

    size_t GetFileCount() {
        std::lock_guard<std::mutex> lock(mutex_);
        return files_.size();
    }
};

// Helper functions for multipart form data parsing
struct MultipartPart {
    std::string name;
    std::string filename;
    std::string content_type;
    std::string content;
};

std::string ExtractBoundary(const std::string& content_type) {
    size_t pos = content_type.find("boundary=");
    if (pos == std::string::npos) {
        return "";
    }
    return content_type.substr(pos + 9);
}

bool ParseMultipartFormData(const std::string& body, const std::string& boundary, std::vector<MultipartPart>& parts) {
    std::string delimiter = "--" + boundary;
    size_t pos = 0;
    
    while (pos < body.length()) {
        // Find next boundary
        size_t boundary_pos = body.find(delimiter, pos);
        if (boundary_pos == std::string::npos) {
            break;
        }
        
        // Check if this is the final boundary
        if (boundary_pos + delimiter.length() + 2 <= body.length() &&
            body.substr(boundary_pos + delimiter.length(), 2) == "--") {
            break;
        }
        
        // Move past boundary and CRLF
        pos = boundary_pos + delimiter.length();
        while (pos < body.length() && (body[pos] == '\r' || body[pos] == '\n')) {
            pos++;
        }
        
        // Find end of headers (empty line)
        size_t header_end = body.find("\r\n\r\n", pos);
        if (header_end == std::string::npos) {
            header_end = body.find("\n\n", pos);
            if (header_end == std::string::npos) {
                break;
            }
            header_end += 2;
        } else {
            header_end += 4;
        }
        
        std::string headers = body.substr(pos, header_end - pos);
        pos = header_end;
        
        // Find next boundary to get content
        size_t next_boundary = body.find(delimiter, pos);
        if (next_boundary == std::string::npos) {
            break;
        }
        
        std::string content = body.substr(pos, next_boundary - pos);
        // Remove trailing CRLF
        while (!content.empty() && (content.back() == '\r' || content.back() == '\n')) {
            content.pop_back();
        }
        
        // Parse headers
        MultipartPart part;
        part.content = content;
        
        // Extract name and filename from Content-Disposition
        size_t name_pos = headers.find("name=\"");
        if (name_pos != std::string::npos) {
            name_pos += 6;
            size_t name_end = headers.find("\"", name_pos);
            part.name = headers.substr(name_pos, name_end - name_pos);
        }
        
        size_t filename_pos = headers.find("filename=\"");
        if (filename_pos != std::string::npos) {
            filename_pos += 10;
            size_t filename_end = headers.find("\"", filename_pos);
            part.filename = headers.substr(filename_pos, filename_end - filename_pos);
        }
        
        // Extract Content-Type
        size_t ct_pos = headers.find("Content-Type: ");
        if (ct_pos != std::string::npos) {
            ct_pos += 14;
            size_t ct_end = headers.find("\r\n", ct_pos);
            if (ct_end == std::string::npos) {
                ct_end = headers.find("\n", ct_pos);
            }
            part.content_type = headers.substr(ct_pos, ct_end - ct_pos);
        }
        
        parts.push_back(part);
        pos = next_boundary;
    }
    
    return !parts.empty();
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

std::string FileListToJson(const std::vector<FileInfo>& files) {
    std::ostringstream oss;
    oss << "[";
    for (size_t i = 0; i < files.size(); ++i) {
        oss << "{\"name\":\"" << files[i].name << "\""
            << ",\"size\":" << files[i].size
            << ",\"size_formatted\":\"" << FormatFileSize(files[i].size) << "\""
            << ",\"upload_time\":\"" << files[i].upload_time << "\""
            << ",\"content_type\":\"" << files[i].content_type << "\"}";
        if (i < files.size() - 1) {
            oss << ",";
        }
    }
    oss << "]";
    return oss.str();
}

int main() {
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

    auto storage = std::make_shared<FileStorage>();
    auto server = quicx::http3::IServer::Create();

    // Logging middleware
    server->AddMiddleware(
        quicx::http3::HttpMethod::kAny,
        quicx::http3::MiddlewarePosition::kBefore,
        [](std::shared_ptr<quicx::http3::IRequest> req, std::shared_ptr<quicx::http3::IResponse> resp) {
            std::cout << "[" << req->GetMethodString() << "] " << req->GetPath() << std::endl;
        }
    );

    // GET / - Welcome page
    server->AddHandler(
        quicx::http3::HttpMethod::kGet,
        "/",
        [](std::shared_ptr<quicx::http3::IRequest> req, std::shared_ptr<quicx::http3::IResponse> resp) {
            std::string html = 
                "<!DOCTYPE html><html><head><title>File Transfer Server</title></head>"
                "<body><h1>QuicX HTTP/3 File Transfer Server</h1>"
                "<p>Available endpoints:</p><ul>"
                "<li>GET /files - List all files</li>"
                "<li>GET /files/:filename - Download file</li>"
                "<li>POST /upload - Upload file</li>"
                "<li>DELETE /files/:filename - Delete file</li>"
                "<li>GET /stats - Server statistics</li>"
                "</ul></body></html>";
            
            resp->AddHeader("Content-Type", "text/html");
            resp->SetBody(html);
            resp->SetStatusCode(200);
        }
    );

    // GET /files - List all files
    server->AddHandler(
        quicx::http3::HttpMethod::kGet,
        "/files",
        [storage](std::shared_ptr<quicx::http3::IRequest> req, std::shared_ptr<quicx::http3::IResponse> resp) {
            auto files = storage->GetFileList();
            std::string json = FileListToJson(files);
            
            resp->AddHeader("Content-Type", "application/json");
            resp->SetBody(json);
            resp->SetStatusCode(200);
            
            std::cout << "  -> Returned " << files.size() << " files" << std::endl;
        }
    );

    // GET /files/:filename - Download file
    server->AddHandler(
        quicx::http3::HttpMethod::kGet,
        "/files/:filename",
        [storage](std::shared_ptr<quicx::http3::IRequest> req, std::shared_ptr<quicx::http3::IResponse> resp) {
            std::string path = req->GetPath();
            size_t last_slash = path.find_last_of('/');
            if (last_slash == std::string::npos) {
                resp->SetStatusCode(400);
                resp->SetBody("{\"error\":\"Invalid path\"}");
                return;
            }
            
            std::string filename = path.substr(last_slash + 1);
            std::string content;
            FileInfo info;
            
            if (storage->LoadFile(filename, content, info)) {
                resp->AddHeader("Content-Type", info.content_type);
                resp->AddHeader("Content-Disposition", "attachment; filename=\"" + filename + "\"");
                resp->SetBody(content);
                resp->SetStatusCode(200);
                
                std::cout << "  -> Downloaded: " << filename 
                          << " (" << FormatFileSize(content.size()) << ")" << std::endl;
            } else {
                resp->SetStatusCode(404);
                resp->SetBody("{\"error\":\"File not found\"}");
                resp->AddHeader("Content-Type", "application/json");
                std::cout << "  -> File not found: " << filename << std::endl;
            }
        }
    );

    // POST /upload - Upload file
    server->AddHandler(
        quicx::http3::HttpMethod::kPost,
        "/upload",
        [storage](std::shared_ptr<quicx::http3::IRequest> req, std::shared_ptr<quicx::http3::IResponse> resp) {
            std::string content_type;
            if (!req->GetHeader("Content-Type", content_type)) {
                resp->SetStatusCode(400);
                resp->SetBody("{\"error\":\"Content-Type header required\"}");
                resp->AddHeader("Content-Type", "application/json");
                return;
            }

            // Check if multipart/form-data
            if (content_type.find("multipart/form-data") != std::string::npos) {
                std::string boundary = ExtractBoundary(content_type);
                if (boundary.empty()) {
                    resp->SetStatusCode(400);
                    resp->SetBody("{\"error\":\"Invalid multipart boundary\"}");
                    resp->AddHeader("Content-Type", "application/json");
                    return;
                }

                std::vector<MultipartPart> parts;
                if (!ParseMultipartFormData(req->GetBody(), boundary, parts)) {
                    resp->SetStatusCode(400);
                    resp->SetBody("{\"error\":\"Failed to parse multipart data\"}");
                    resp->AddHeader("Content-Type", "application/json");
                    return;
                }

                // Find file part
                for (const auto& part : parts) {
                    if (!part.filename.empty()) {
                        std::string file_content_type = part.content_type.empty() ? 
                            "application/octet-stream" : part.content_type;
                        
                        if (storage->SaveFile(part.filename, part.content, file_content_type)) {
                            std::ostringstream oss;
                            oss << "{\"message\":\"File uploaded successfully\""
                                << ",\"filename\":\"" << part.filename << "\""
                                << ",\"size\":" << part.content.size()
                                << ",\"size_formatted\":\"" << FormatFileSize(part.content.size()) << "\"}";
                            
                            resp->AddHeader("Content-Type", "application/json");
                            resp->SetBody(oss.str());
                            resp->SetStatusCode(201);
                            
                            std::cout << "  -> Uploaded: " << part.filename 
                                      << " (" << FormatFileSize(part.content.size()) << ")" << std::endl;
                            return;
                        }
                    }
                }

                resp->SetStatusCode(400);
                resp->SetBody("{\"error\":\"No file found in request\"}");
                resp->AddHeader("Content-Type", "application/json");
            } else {
                // Direct binary upload with filename in query or header
                std::string filename;
                if (!req->GetHeader("X-Filename", filename)) {
                    // Try to get from path
                    std::string path = req->GetPath();
                    size_t query_pos = path.find('?');
                    if (query_pos != std::string::npos) {
                        std::string query = path.substr(query_pos + 1);
                        size_t name_pos = query.find("name=");
                        if (name_pos != std::string::npos) {
                            filename = query.substr(name_pos + 5);
                            size_t amp_pos = filename.find('&');
                            if (amp_pos != std::string::npos) {
                                filename = filename.substr(0, amp_pos);
                            }
                        }
                    }
                }

                if (filename.empty()) {
                    filename = "uploaded_file_" + std::to_string(time(nullptr));
                }

                if (storage->SaveFile(filename, req->GetBody(), content_type)) {
                    std::ostringstream oss;
                    oss << "{\"message\":\"File uploaded successfully\""
                        << ",\"filename\":\"" << filename << "\""
                        << ",\"size\":" << req->GetBody().size()
                        << ",\"size_formatted\":\"" << FormatFileSize(req->GetBody().size()) << "\"}";
                    
                    resp->AddHeader("Content-Type", "application/json");
                    resp->SetBody(oss.str());
                    resp->SetStatusCode(201);
                    
                    std::cout << "  -> Uploaded: " << filename 
                              << " (" << FormatFileSize(req->GetBody().size()) << ")" << std::endl;
                } else {
                    resp->SetStatusCode(500);
                    resp->SetBody("{\"error\":\"Failed to save file\"}");
                    resp->AddHeader("Content-Type", "application/json");
                }
            }
        }
    );

    // DELETE /files/:filename - Delete file
    server->AddHandler(
        quicx::http3::HttpMethod::kDelete,
        "/files/:filename",
        [storage](std::shared_ptr<quicx::http3::IRequest> req, std::shared_ptr<quicx::http3::IResponse> resp) {
            std::string path = req->GetPath();
            size_t last_slash = path.find_last_of('/');
            if (last_slash == std::string::npos) {
                resp->SetStatusCode(400);
                resp->SetBody("{\"error\":\"Invalid path\"}");
                return;
            }
            
            std::string filename = path.substr(last_slash + 1);
            
            if (storage->DeleteFile(filename)) {
                resp->SetStatusCode(204);
                resp->SetBody("");
                std::cout << "  -> Deleted: " << filename << std::endl;
            } else {
                resp->SetStatusCode(404);
                resp->SetBody("{\"error\":\"File not found\"}");
                resp->AddHeader("Content-Type", "application/json");
                std::cout << "  -> Delete failed: " << filename << std::endl;
            }
        }
    );

    // GET /stats - Server statistics
    server->AddHandler(
        quicx::http3::HttpMethod::kGet,
        "/stats",
        [storage](std::shared_ptr<quicx::http3::IRequest> req, std::shared_ptr<quicx::http3::IResponse> resp) {
            std::ostringstream oss;
            oss << "{\"total_files\":" << storage->GetFileCount()
                << ",\"total_size\":" << storage->GetTotalSize()
                << ",\"total_size_formatted\":\"" << FormatFileSize(storage->GetTotalSize()) << "\"}";
            
            resp->AddHeader("Content-Type", "application/json");
            resp->SetBody(oss.str());
            resp->SetStatusCode(200);
        }
    );

    // CORS middleware
    server->AddMiddleware(
        quicx::http3::HttpMethod::kAny,
        quicx::http3::MiddlewarePosition::kAfter,
        [](std::shared_ptr<quicx::http3::IRequest> req, std::shared_ptr<quicx::http3::IResponse> resp) {
            resp->AddHeader("X-Powered-By", "QuicX-HTTP3");
            resp->AddHeader("Access-Control-Allow-Origin", "*");
        }
    );

    // Configure and start server
    quicx::http3::Http3ServerConfig config;
    config.cert_pem_ = cert_pem;
    config.key_pem_ = key_pem;
    config.config_.thread_num_ = 2;
    config.config_.log_level_ = quicx::http3::LogLevel::kError;
    
    server->Init(config);
    
    std::cout << "==================================" << std::endl;
    std::cout << "File Transfer Server Starting..." << std::endl;
    std::cout << "==================================" << std::endl;
    std::cout << "Listen on: https://0.0.0.0:8884" << std::endl;
    std::cout << "Storage directory: " << STORAGE_DIR << std::endl;
    std::cout << std::endl;
    std::cout << "Available endpoints:" << std::endl;
    std::cout << "  GET    /              - Welcome page" << std::endl;
    std::cout << "  GET    /files         - List all files" << std::endl;
    std::cout << "  GET    /files/:name   - Download file" << std::endl;
    std::cout << "  POST   /upload        - Upload file" << std::endl;
    std::cout << "  DELETE /files/:name   - Delete file" << std::endl;
    std::cout << "  GET    /stats         - Server statistics" << std::endl;
    std::cout << "==================================" << std::endl;
    std::cout << std::endl;

    if (!server->Start("0.0.0.0", 8884)) {
        std::cout << "Failed to start server" << std::endl;
        return 1;
    }
    
    server->Join();
    return 0;
}

