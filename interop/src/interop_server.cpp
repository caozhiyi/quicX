/**
 * @file interop_server.cpp
 * @brief QUIC Interop Test Server for quicX
 *
 * This server is designed to work with the quic-interop-runner framework.
 * It serves files over QUIC/HTTP3 and supports various test scenarios.
 *
 * Environment Variables:
 * - PORT: Port to listen on (default: 443)
 * - WWW: Root directory for files (default: /www)
 * - QLOGDIR: Directory for qlog output
 * - SSLKEYLOGFILE: File for TLS key logging
 */

#include <iostream>
#include <string>
#include <cstdlib>
#include <signal.h>
#include <fstream>
#include <vector>
#include <thread>
#include <chrono>

#include "http3/include/if_server.h"
#include "http3/include/if_request.h"
#include "http3/include/if_response.h"

using namespace quicx;

class InteropServer {
private:
    std::shared_ptr<IServer> server_;
    std::string root_dir_;
    uint16_t port_;
    bool running_;
    FILE* keylog_file_;

public:
    InteropServer(const std::string& root_dir, uint16_t port)
        : root_dir_(root_dir), port_(port), running_(true), keylog_file_(nullptr) {}

    ~InteropServer() {
        if (keylog_file_) {
            fclose(keylog_file_);
        }
    }

    bool Init(const std::string& cert_file, const std::string& key_file, const char* keylog_path = nullptr) {
        // Open SSLKEYLOG file if specified
        if (keylog_path) {
            keylog_file_ = fopen(keylog_path, "a");
            if (keylog_file_) {
                std::cout << "SSLKEYLOG enabled: " << keylog_path << std::endl;
            } else {
                std::cerr << "Warning: Failed to open SSLKEYLOG file: " << keylog_path << std::endl;
            }
        }

        server_ = IServer::Create();

        Http3ServerConfig server_config;
        server_config.cert_file_ = cert_file;
        server_config.key_file_ = key_file;
        server_config.config_.thread_num_ = 4;
        server_config.config_.log_level_ = LogLevel::kInfo;

        // Enable ECN if requested
        const char* enable_ecn = std::getenv("ENABLE_ECN");
        if (enable_ecn && std::atoi(enable_ecn) == 1) {
            server_config.config_.enable_ecn_ = true;
            std::cout << "ECN enabled" << std::endl;
        }

        if (!server_->Init(server_config)) {
            std::cerr << "Failed to initialize server" << std::endl;
            return false;
        }

        // Register file serving handler - match any path
        server_->AddHandler(HttpMethod::kGet, "/*",
            [this](std::shared_ptr<IRequest> req, std::shared_ptr<IResponse> resp) {
                this->ServeFile(req, resp);
            });

        std::cout << "Server initialized on port " << port_ << std::endl;
        std::cout << "Serving files from: " << root_dir_ << std::endl;

        return true;
    }

    void ServeFile(std::shared_ptr<IRequest> req, std::shared_ptr<IResponse> resp) {
        // Get the path from the request (e.g., "/1MB.bin")
        std::string path = req->GetPath();
        std::string filepath = root_dir_ + path;

        std::cout << "Serving file: " << filepath << std::endl;

        // Open file
        std::ifstream file(filepath, std::ios::binary | std::ios::ate);
        if (!file.is_open()) {
            std::cerr << "File not found: " << filepath << std::endl;
            resp->SetStatusCode(404);
            resp->AddHeader("content-type", "text/plain");
            resp->AppendBody("File not found: " + path);
            return;
        }

        // Get file size
        std::streamsize size = file.tellg();
        file.seekg(0, std::ios::beg);

        // Read file content
        std::vector<char> buffer(size);
        if (!file.read(buffer.data(), size)) {
            std::cerr << "Failed to read file: " << filepath << std::endl;
            resp->SetStatusCode(500);
            resp->AddHeader("content-type", "text/plain");
            resp->AppendBody("Failed to read file: " + path);
            return;
        }

        file.close();

        std::cout << "File size: " << size << " bytes" << std::endl;

        // Send response
        resp->SetStatusCode(200);
        resp->AddHeader("content-type", "application/octet-stream");
        resp->AddHeader("content-length", std::to_string(size));
        resp->AppendBody(reinterpret_cast<const uint8_t*>(buffer.data()), size);

        std::cout << "File served successfully: " << path << std::endl;
    }

    bool Start() {
        if (!server_->Start("0.0.0.0", port_)) {
            std::cerr << "Failed to start server" << std::endl;
            return false;
        }

        std::cout << "Server listening on https://0.0.0.0:" << port_ << std::endl;

        // Wait for server
        server_->Join();

        return true;
    }

    void Stop() {
        running_ = false;
        if (server_) {
            server_->Stop();
        }
    }
};

static InteropServer* g_server = nullptr;

void signal_handler(int signum) {
    std::cout << "\nReceived signal " << signum << ", shutting down..." << std::endl;
    if (g_server) {
        g_server->Stop();
    }
}

int main(int argc, char* argv[]) {
    // Register signal handlers
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    // Read configuration from environment
    const char* port_env = std::getenv("PORT");
    const char* www_env = std::getenv("WWW");
    const char* qlog_env = std::getenv("QLOGDIR");
    const char* keylog_env = std::getenv("SSLKEYLOGFILE");

    uint16_t port = port_env ? std::atoi(port_env) : 443;
    std::string www_dir = www_env ? www_env : "/www";

    std::cout << "========================================" << std::endl;
    std::cout << "quicX QUIC Interop Test Server" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Port: " << port << std::endl;
    std::cout << "WWW: " << www_dir << std::endl;
    if (qlog_env) {
        std::cout << "QLOG: " << qlog_env << std::endl;
    }
    if (keylog_env) {
        std::cout << "KEYLOG: " << keylog_env << std::endl;
    }
    std::cout << "========================================" << std::endl;
    std::cout << std::endl;

    std::string cert_file = "/certs/cert.pem";
    std::string key_file = "/certs/key.pem";

    InteropServer server(www_dir, port);
    g_server = &server;

    if (!server.Init(cert_file, key_file, keylog_env)) {
        return 1;
    }

    if (!server.Start()) {
        return 1;
    }

    std::cout << "Server stopped" << std::endl;
    return 0;
}
