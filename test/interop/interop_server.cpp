/**
 * @file interop_server.cpp
 * @brief QUIC Interop Test Server for quicX (hq-interop protocol)
 *
 * Implements the hq-interop protocol (HTTP/0.9 over QUIC) directly on top of
 * the QUIC transport layer. This is the standard protocol used by
 * quic-interop-runner for non-HTTP/3 test cases.
 *
 * Protocol:
 *   Request:  "GET /path\r\n" on a client-initiated bidirectional stream
 *   Response: raw file bytes on the same stream, then FIN
 *
 * Environment Variables:
 *   PORT           - Port to listen on (default: 443)
 *   WWW            - Root directory for files (default: /www)
 *   QLOGDIR        - Directory for qlog output
 *   SSLKEYLOGFILE  - File for TLS key logging
 *   RETRY          - "1" to force retry
 *   CIPHER_SUITE   - TLS cipher suite override
 *   QUIC_VERSION   - QUIC version hex (e.g. 0x6b3343cf for v2)
 */

#include <signal.h>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include "quic/include/if_quic_server.h"
#include "quic/include/if_quic_connection.h"
#include "quic/include/if_quic_bidirection_stream.h"

using namespace quicx;

static const std::string kHqInteropAlpn = "hq-interop";
static const size_t kSendChunkSize = 16384;  // 16KB send buffer

struct StreamContext {
    std::shared_ptr<IQuicBidirectionStream> stream;
    std::string request_buffer;
    std::string www_root;
    bool request_parsed = false;
};

class HqInteropServer {
public:
    HqInteropServer(const std::string& root_dir, uint16_t port)
        : root_dir_(root_dir), port_(port) {}

    bool Init(const std::string& cert_file, const std::string& key_file) {
        QuicTransportParams transport_params;
        quic_ = IQuicServer::Create(transport_params);

        quic_->SetConnectionStateCallBack(
            [this](std::shared_ptr<IQuicConnection> conn, ConnectionOperation op,
                   uint32_t error, const std::string& reason) {
                OnConnection(conn, op, error, reason);
            });

        QuicServerConfig config;
        config.cert_file_ = cert_file;
        config.key_file_ = key_file;
        config.alpn_ = kHqInteropAlpn;
        config.config_.worker_thread_num_ = 4;
        config.config_.log_level_ = LogLevel::kDebug;
        config.config_.log_path_ = "./logs";  // Current directory for logs

        // QLog
        const char* qlog_dir = std::getenv("QLOGDIR");
        if (qlog_dir) {
            config.config_.qlog_config_.enabled = true;
            config.config_.qlog_config_.output_dir = qlog_dir;
            std::cout << "QLog enabled, output: " << qlog_dir << std::endl;
        }

        // SSLKEYLOG
        const char* keylog = std::getenv("SSLKEYLOGFILE");
        if (keylog) {
            config.config_.keylog_file_ = keylog;
            std::cout << "SSLKEYLOG enabled: " << keylog << std::endl;
        }

        // ECN
        const char* ecn = std::getenv("ENABLE_ECN");
        if (ecn && std::atoi(ecn) == 1) {
            config.config_.enable_ecn_ = true;
            std::cout << "ECN enabled" << std::endl;
        }

        // Retry
        const char* retry = std::getenv("RETRY");
        if (retry && std::atoi(retry) == 1) {
            config.retry_policy_ = RetryPolicy::ALWAYS;
            std::cout << "Retry enabled (force_retry mode)" << std::endl;
        }

        // 0-RTT / Early Data
        const char* zerortt = std::getenv("ENABLE_0RTT");
        if (zerortt && std::atoi(zerortt) == 1) {
            config.config_.enable_0rtt_ = true;
            std::cout << "0-RTT enabled" << std::endl;
        }

        // Session Resumption (BoringSSL sends NewSessionTicket by default,
        // but we log it for clarity)
        const char* resumption = std::getenv("ENABLE_RESUMPTION");
        if (resumption && std::atoi(resumption) == 1) {
            std::cout << "Session Resumption enabled" << std::endl;
        }

        // Cipher Suites
        const char* ciphers = std::getenv("CIPHER_SUITE");
        if (ciphers) {
            config.config_.cipher_suites_ = ciphers;
            std::cout << "Cipher Suites: " << ciphers << std::endl;
        }

        // QUIC Version
        const char* quic_version = std::getenv("QUIC_VERSION");
        if (quic_version) {
            uint32_t version = static_cast<uint32_t>(std::strtoul(quic_version, nullptr, 0));
            config.config_.quic_version_ = version;
            std::cout << "QUIC Version: 0x" << std::hex << version << std::dec << std::endl;
        } else {
            config.config_.quic_version_ = 0x00000001;
            std::cout << "QUIC Version: v1 (0x00000001) [default for interop]" << std::endl;
        }

        if (!quic_->Init(config)) {
            std::cerr << "Failed to initialize QUIC server" << std::endl;
            return false;
        }

        std::cout << "Server initialized on port " << port_ << std::endl;
        std::cout << "Serving files from: " << root_dir_ << std::endl;
        std::cout << "ALPN: " << kHqInteropAlpn << std::endl;
        return true;
    }

    bool Start() {
        // Use "::" to listen on all interfaces (both IPv4 and IPv6) with dual-stack socket
        if (!quic_->ListenAndAccept("::", port_)) {
            std::cerr << "Failed to start listening" << std::endl;
            return false;
        }
        std::cout << "Server listening on [::]:" << port_ << std::endl;
        quic_->Join();
        return true;
    }

    void Stop() {
        if (quic_) {
            quic_->AddTimer(500, [this]() {
                if (quic_) {
                    quic_->Destroy();
                }
            });
        }
    }

private:
    void OnConnection(std::shared_ptr<IQuicConnection> conn, ConnectionOperation op,
                      uint32_t error, const std::string& reason) {
        if (op == ConnectionOperation::kConnectionClose) {
            std::string addr;
            uint32_t port;
            conn->GetRemoteAddr(addr, port);
            std::cout << "Connection closed from " << addr << ":" << port
                      << " error=" << error << " reason=" << reason << std::endl;
            return;
        }

        // New connection
        std::string addr;
        uint32_t port;
        conn->GetRemoteAddr(addr, port);
        std::cout << "New connection from " << addr << ":" << port << std::endl;

        conn->SetStreamStateCallBack(
            [this](std::shared_ptr<IQuicStream> stream, uint32_t err) {
                OnStream(stream, err);
            });
    }

    void OnStream(std::shared_ptr<IQuicStream> stream, uint32_t error) {
        if (error != 0) {
            std::cerr << "Stream error: " << error << std::endl;
            return;
        }

        auto bidi = std::dynamic_pointer_cast<IQuicBidirectionStream>(stream);
        if (!bidi) {
            std::cerr << "Received non-bidirectional stream, ignoring" << std::endl;
            return;
        }

        auto ctx = std::make_shared<StreamContext>();
        ctx->stream = bidi;
        ctx->www_root = root_dir_;

        bidi->SetStreamReadCallBack(
            [ctx](std::shared_ptr<IBufferRead> data, bool is_last, uint32_t err) {
                OnStreamData(ctx, data, is_last, err);
            });
    }

    static void OnStreamData(std::shared_ptr<StreamContext> ctx,
                             std::shared_ptr<IBufferRead> data, bool is_last, uint32_t error) {
        if (error != 0) {
            std::cerr << "Stream read error: " << error << std::endl;
            return;
        }

        if (ctx->request_parsed) {
            return;
        }

        // Read available data into request buffer
        if (data) {
            uint32_t len = data->GetDataLength();
            if (len > 0) {
                std::vector<uint8_t> buf(len);
                uint32_t read = data->Read(buf.data(), len);
                ctx->request_buffer.append(reinterpret_cast<char*>(buf.data()), read);
            }
        }

        // Check if we have a complete request (ends with \r\n)
        auto pos = ctx->request_buffer.find("\r\n");
        if (pos == std::string::npos) {
            if (is_last) {
                std::cerr << "Incomplete request (no \\r\\n before FIN)" << std::endl;
            }
            return;
        }

        ctx->request_parsed = true;

        // Parse "GET /path\r\n"
        std::string request_line = ctx->request_buffer.substr(0, pos);
        std::string path;

        if (request_line.substr(0, 4) == "GET ") {
            path = request_line.substr(4);
        } else {
            std::cerr << "Invalid request: " << request_line << std::endl;
            ctx->stream->Close();
            return;
        }

        // Sanitize path
        if (path.empty() || path[0] != '/') {
            path = "/" + path;
        }

        std::string filepath = ctx->www_root + path;
        std::cout << "Serving: " << filepath << std::endl;

        // Open and send file
        FILE* file = fopen(filepath.c_str(), "rb");
        if (!file) {
            std::cerr << "File not found: " << filepath << std::endl;
            ctx->stream->Close();
            return;
        }

        // Get file size for logging
        fseek(file, 0, SEEK_END);
        long file_size = ftell(file);
        fseek(file, 0, SEEK_SET);
        std::cout << "File size: " << file_size << " bytes" << std::endl;

        // Send file content in chunks
        uint8_t buf[kSendChunkSize];
        size_t total_sent = 0;
        while (true) {
            size_t bytes_read = fread(buf, 1, kSendChunkSize, file);
            if (bytes_read == 0) {
                break;
            }
            int32_t sent = ctx->stream->Send(buf, static_cast<uint32_t>(bytes_read));
            if (sent < 0) {
                std::cerr << "Send failed" << std::endl;
                break;
            }
            total_sent += bytes_read;
        }

        fclose(file);
        std::cout << "Sent " << total_sent << " bytes for " << path << std::endl;

        // Close stream (sends FIN)
        ctx->stream->Close();
    }

    std::shared_ptr<IQuicServer> quic_;
    std::string root_dir_;
    uint16_t port_;
};

static HqInteropServer* g_server = nullptr;

void signal_handler(int signum) {
    std::cout << "\nReceived signal " << signum << ", shutting down..." << std::endl;
    if (g_server) {
        g_server->Stop();
    }
}

int main(int argc, char* argv[]) {
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    // Parse command-line arguments
    uint16_t port = 443;
    std::string www_dir = "/www";
    std::string cert_file = "/certs/cert.pem";
    std::string key_file = "/certs/priv.key";
    std::string qlog_dir;
    bool force_retry = false;
    bool enable_resumption = false;
    bool enable_0rtt = false;
    bool enable_keyupdate = false;
    std::string cipher_suite;
    uint32_t quic_version = 0;
    bool strict_version = false;
    
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--port" && i + 1 < argc) {
            port = std::atoi(argv[++i]);
        } else if (arg == "--root" && i + 1 < argc) {
            www_dir = argv[++i];
        } else if (arg == "--cert" && i + 1 < argc) {
            cert_file = argv[++i];
        } else if (arg == "--key" && i + 1 < argc) {
            key_file = argv[++i];
        } else if (arg == "--qlog-dir" && i + 1 < argc) {
            qlog_dir = argv[++i];
        } else if (arg == "--force-retry") {
            force_retry = true;
        } else if (arg == "--enable-resumption") {
            enable_resumption = true;
        } else if (arg == "--enable-0rtt") {
            enable_0rtt = true;
        } else if (arg == "--enable-keyupdate") {
            enable_keyupdate = true;
        } else if (arg == "--strict-version") {
            strict_version = true;
        } else if (arg == "--cipher" && i + 1 < argc) {
            cipher_suite = argv[++i];
        } else if (arg == "--quic-version" && i + 1 < argc) {
            quic_version = static_cast<uint32_t>(std::strtoul(argv[++i], nullptr, 0));
        }
    }
    
    // Fall back to environment variables if not provided via command line
    const char* port_env = std::getenv("PORT");
    const char* www_env = std::getenv("WWW");
    const char* cert_env = std::getenv("CERT_FILE");
    const char* key_env = std::getenv("KEY_FILE");
    const char* qlog_env = std::getenv("QLOGDIR");
    
    if (port == 443 && port_env) {
        port = std::atoi(port_env);
    }
    if (www_dir == "/www" && www_env) {
        www_dir = www_env;
    }
    if (cert_file == "/certs/cert.pem" && cert_env) {
        cert_file = cert_env;
    }
    if (key_file == "/certs/priv.key" && key_env) {
        key_file = key_env;
    }
    if (qlog_dir.empty() && qlog_env) {
        qlog_dir = qlog_env;
    }
    
    // Apply command-line parameters via environment variables for Init()
    if (force_retry) {
        setenv("RETRY", "1", 1);
    }
    if (enable_resumption) {
        setenv("ENABLE_RESUMPTION", "1", 1);
    }
    if (enable_0rtt) {
        setenv("ENABLE_0RTT", "1", 1);
    }
    if (enable_keyupdate) {
        setenv("ENABLE_KEYUPDATE", "1", 1);
    }
    if (!cipher_suite.empty()) {
        setenv("CIPHER_SUITE", cipher_suite.c_str(), 1);
    }
    if (quic_version > 0) {
        char version_buf[32];
        snprintf(version_buf, sizeof(version_buf), "0x%08x", quic_version);
        setenv("QUIC_VERSION", version_buf, 1);
    }
    if (!qlog_dir.empty()) {
        setenv("QLOGDIR", qlog_dir.c_str(), 1);
    }

    std::cout << "========================================" << std::endl;
    std::cout << "quicX hq-interop Server" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Port: " << port << std::endl;
    std::cout << "WWW: " << www_dir << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << std::endl;

    std::cout << "Certificate: " << cert_file << std::endl;
    std::cout << "Private Key: " << key_file << std::endl;

    HqInteropServer server(www_dir, port);
    g_server = &server;

    if (!server.Init(cert_file, key_file)) {
        return 1;
    }

    if (!server.Start()) {
        return 1;
    }

    std::cout << "Server stopped" << std::endl;
    return 0;
}
