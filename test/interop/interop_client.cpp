/**
 * @file interop_client.cpp
 * @brief QUIC Interop Test Client for quicX (hq-interop protocol)
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
 *   SERVER         - Server hostname
 *   PORT           - Server port (default: 443)
 *   REQUESTS       - Space-separated URLs to download
 *   DOWNLOADS      - Download directory (default: /downloads)
 *   QLOGDIR        - Directory for qlog output
 *   SSLKEYLOGFILE  - File for TLS key logging
 *   TESTCASE       - Test case name
 *   QUIC_VERSION   - QUIC version hex
 *   CIPHER_SUITE   - TLS cipher suite override
 *   ZERORTT        - "1" to enable 0-RTT
 *   SESSION_FILE   - Path for session resumption file
 *   KEY_UPDATE     - "1" to enable key update
 *   ENABLE_ECN     - "1" to enable ECN
 */

#include <signal.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <condition_variable>
#include <iostream>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "quic/include/if_quic_client.h"
#include "quic/include/if_quic_connection.h"
#include "quic/include/if_quic_bidirection_stream.h"
#include "http3/include/if_client.h"
#include "http3/include/if_async_handler.h"
#include "http3/include/if_request.h"
#include "http3/include/if_response.h"

using namespace quicx;

static const std::string kHqInteropAlpn = "hq-interop";

// Resolve hostname to IP address (prefer IPv4)
static std::string ResolveHost(const std::string& host) {
    struct addrinfo hints, *result, *rp;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;

    int ret = getaddrinfo(host.c_str(), nullptr, &hints, &result);
    if (ret != 0) {
        std::cerr << "DNS resolution failed for " << host << ": " << gai_strerror(ret) << std::endl;
        return "";
    }

    // Prefer IPv4 (AF_INET) over IPv6 (AF_INET6)
    char ipv4[INET_ADDRSTRLEN] = "";
    char ipv6[INET6_ADDRSTRLEN] = "";
    
    for (rp = result; rp != nullptr; rp = rp->ai_next) {
        if (rp->ai_family == AF_INET && ipv4[0] == '\0') {
            struct sockaddr_in* addr = reinterpret_cast<struct sockaddr_in*>(rp->ai_addr);
            inet_ntop(AF_INET, &addr->sin_addr, ipv4, sizeof(ipv4));
        } else if (rp->ai_family == AF_INET6 && ipv6[0] == '\0') {
            struct sockaddr_in6* addr = reinterpret_cast<struct sockaddr_in6*>(rp->ai_addr);
            inet_ntop(AF_INET6, &addr->sin6_addr, ipv6, sizeof(ipv6));
        }
    }

    freeaddrinfo(result);
    
    // Return IPv4 if available, otherwise IPv6
    if (ipv4[0] != '\0') {
        return std::string(ipv4);
    } else if (ipv6[0] != '\0') {
        return std::string(ipv6);
    }
    return "";
}

struct DownloadContext {
    std::string filepath;
    FILE* file = nullptr;
    size_t bytes_received = 0;
    bool done = false;
    bool success = false;
    std::mutex mtx;
    std::condition_variable cv;

    ~DownloadContext() {
        if (file) {
            fclose(file);
            file = nullptr;
        }
    }
};

class HqInteropClient {
public:
    HqInteropClient(const std::string& download_dir, const std::string& qlog_dir = "")
        : download_dir_(download_dir), qlog_dir_(qlog_dir) {}

    ~HqInteropClient() {
        Shutdown();
    }

    bool Init() {
        QuicTransportParams transport_params;
        // Use a short idle timeout for interop testing so the connection closes
        // promptly after all streams finish, well within container timeout.
        transport_params.max_idle_timeout_ms_ = 10000;  // 10 seconds
        quic_ = IQuicClient::Create(transport_params);
        if (!quic_) {
            std::cerr << "Failed to create QUIC client" << std::endl;
            return false;
        }

        quic_->SetConnectionStateCallBack(
            [this](std::shared_ptr<IQuicConnection> conn, ConnectionOperation op,
                   uint32_t error, const std::string& reason) {
                OnConnection(conn, op, error, reason);
            });

        QuicClientConfig config;
        config.verify_peer_ = false;  // Interop tests use self-signed certificates
        config.config_.worker_thread_num_ = 4;
        config.config_.log_level_ = LogLevel::kDebug;
        config.config_.log_path_ = "./logs";  // Current directory for logs

        // QLog
        if (!qlog_dir_.empty()) {
            config.config_.qlog_config_.enabled = true;
            config.config_.qlog_config_.output_dir = qlog_dir_;
            std::cout << "QLog enabled, output: " << qlog_dir_ << std::endl;
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

        // ZeroRTT
        const char* zerortt = std::getenv("ZERORTT");
        if (zerortt && std::atoi(zerortt) == 1) {
            config.config_.enable_0rtt_ = true;
            std::cout << "ZeroRTT enabled" << std::endl;
        }

        // Session Resumption
        const char* session_file = std::getenv("SESSION_FILE");
        if (session_file) {
            config.enable_session_cache_ = true;
            config.session_cache_path_ = session_file;
            std::cout << "Session Resumption enabled, file: " << session_file << std::endl;
        }

        // Cipher Suites
        const char* ciphers = std::getenv("CIPHER_SUITE");
        if (ciphers) {
            config.config_.cipher_suites_ = ciphers;
            std::cout << "Cipher Suites: " << ciphers << std::endl;
        }

        // Key Update
        const char* key_update = std::getenv("KEY_UPDATE");
        if (key_update && std::atoi(key_update) == 1) {
            config.config_.enable_key_update_ = true;
            std::cout << "Key Update enabled" << std::endl;
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
            std::cerr << "Failed to initialize QUIC client" << std::endl;
            return false;
        }

        std::cout << "Client initialized" << std::endl;
        std::cout << "Download directory: " << download_dir_ << std::endl;
        return true;
    }

    bool Connect(const std::string& server, uint16_t port, int32_t timeout_ms = 30000) {
        std::cout << "Connecting to " << server << ":" << port << "..." << std::endl;

        // Resolve hostname to IP address (QUIC Connection() requires IP, not hostname)
        std::string ip = ResolveHost(server);
        if (ip.empty()) {
            std::cerr << "Failed to resolve hostname: " << server << std::endl;
            return false;
        }
        std::cout << "Resolved " << server << " -> " << ip << std::endl;

        if (!quic_->Connection(ip, port, kHqInteropAlpn, timeout_ms, "", server)) {
            std::cerr << "Failed to initiate connection" << std::endl;
            return false;
        }

        // Wait for connection to be established
        std::unique_lock<std::mutex> lock(conn_mtx_);
        if (!conn_cv_.wait_for(lock, std::chrono::seconds(30), [this] { return conn_ready_ || conn_failed_; })) {
            std::cerr << "Connection timeout" << std::endl;
            return false;
        }

        if (conn_failed_) {
            std::cerr << "Connection failed" << std::endl;
            return false;
        }

        std::cout << "Connection established" << std::endl;
        return true;
    }

    bool DownloadFile(const std::string& url) {
        // Extract path from URL (e.g., "https://server:port/file.bin" -> "/file.bin")
        std::string path = ExtractPath(url);
        std::string filename = ExtractFilename(url);
        std::string filepath = download_dir_ + "/" + filename;

        std::cout << "Downloading: " << url << " -> " << filepath << std::endl;

        std::shared_ptr<IQuicConnection> conn;
        {
            std::lock_guard<std::mutex> lock(conn_mtx_);
            conn = conn_;
        }

        if (!conn) {
            std::cerr << "No active connection" << std::endl;
            return false;
        }

        // Create bidirectional stream (retry if max_streams reached)
        std::shared_ptr<IQuicStream> stream;
        for (int retry = 0; retry < 30; retry++) {
            stream = conn->MakeStream(StreamDirection::kBidi);
            if (stream) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
        if (!stream) {
            std::cerr << "Failed to create stream after retries" << std::endl;
            return false;
        }

        auto bidi = std::dynamic_pointer_cast<IQuicBidirectionStream>(stream);
        if (!bidi) {
            std::cerr << "Failed to cast to bidirectional stream" << std::endl;
            return false;
        }

        // Prepare download context
        auto ctx = std::make_shared<DownloadContext>();
        ctx->filepath = filepath;
        ctx->file = fopen(filepath.c_str(), "wb");
        if (!ctx->file) {
            std::cerr << "Failed to open file for writing: " << filepath << std::endl;
            return false;
        }

        // Set read callback to receive response data
        // Note: Keep ctx alive by capturing it in lambda
        bidi->SetStreamReadCallBack(
            [ctx](std::shared_ptr<IBufferRead> data, bool is_last, uint32_t error) {
                // Debug: Log every callback invocation
                static std::atomic<int> call_count{0};
                int count = ++call_count;
                std::cout << "[CALLBACK #" << count << "] data=" << (data ? data->GetDataLength() : 0) 
                          << " bytes, is_last=" << is_last << ", error=" << error << std::endl;

                if (error != 0) {
                    std::cerr << "Stream read error: " << error << std::endl;
                    std::lock_guard<std::mutex> lock(ctx->mtx);
                    ctx->done = true;
                    ctx->cv.notify_all();
                    return;
                }

                if (data) {
                    uint32_t len = data->GetDataLength();
                    if (len > 0) {
                        std::vector<uint8_t> buf(len);
                        uint32_t read = data->Read(buf.data(), len);
                        if (ctx->file && read > 0) {
                            size_t written = fwrite(buf.data(), 1, read, ctx->file);
                            ctx->bytes_received += written;
                        }
                    }
                }

                if (is_last) {
                    if (ctx->file) {
                        fclose(ctx->file);
                        ctx->file = nullptr;
                    }
                    std::cout << "Downloaded " << ctx->bytes_received << " bytes -> "
                              << ctx->filepath << std::endl;
                    std::lock_guard<std::mutex> lock(ctx->mtx);
                    ctx->success = true;
                    ctx->done = true;
                    ctx->cv.notify_all();
                }
            });

        // Send hq-interop request: "GET /path\r\n"
        std::string request = "GET " + path + "\r\n";
        int32_t sent = bidi->Send(reinterpret_cast<uint8_t*>(request.data()),
                                  static_cast<uint32_t>(request.size()));
        if (sent < 0) {
            std::cerr << "Failed to send request" << std::endl;
            return false;
        }

        // Flush to ensure request is sent immediately
        if (!bidi->Flush()) {
            std::cerr << "Failed to flush stream" << std::endl;
        }

        // Close the send direction (sends FIN) to signal end of request.
        // hq-interop protocol: server waits for client FIN before responding.
        bidi->Close();

        std::cout << "Sent request: GET " << path << std::endl;

        // Wait for download to complete
        {
            std::unique_lock<std::mutex> lock(ctx->mtx);
            if (!ctx->cv.wait_for(lock, std::chrono::seconds(30), [&ctx] { return ctx->done; })) {
                std::cerr << "Download timeout for " << url << std::endl;
                return false;
            }
        }

        return ctx->success;
    }

    bool DownloadAll(const std::vector<std::string>& urls) {
        if (urls.size() <= 1) {
            // Single file: just download it
            for (const auto& url : urls) {
                if (!DownloadFile(url)) {
                    std::cerr << "Failed to download: " << url << std::endl;
                    return false;
                }
            }
            std::cout << "All " << urls.size() << " downloads completed" << std::endl;
            return true;
        }
        
        // Multiple files: send all requests concurrently on a single connection
        // All stream creation and sends happen on THIS thread (serially) to avoid
        // thread-safety issues with MakeStream. The QUIC stack handles I/O
        // asynchronously via its event loop.
        std::cout << "Starting " << urls.size() << " concurrent downloads..." << std::endl;
        
        std::shared_ptr<IQuicConnection> conn;
        {
            std::lock_guard<std::mutex> lock(conn_mtx_);
            conn = conn_;
        }
        if (!conn) {
            std::cerr << "No active connection" << std::endl;
            return false;
        }
        
        // Shared state for tracking all downloads
        struct AllDownloadsState {
            std::mutex mtx;
            std::condition_variable cv;
            std::atomic<int> completed{0};
            std::atomic<int> succeeded{0};
            int total{0};
        };
        auto state = std::make_shared<AllDownloadsState>();
        state->total = static_cast<int>(urls.size());
        
        int initiated = 0;
        for (size_t i = 0; i < urls.size(); i++) {
            const auto& url = urls[i];
            std::string path = ExtractPath(url);
            std::string filename = ExtractFilename(url);
            std::string filepath = download_dir_ + "/" + filename;
            
            std::cout << "Downloading: " << url << " -> " << filepath << std::endl;
            
            // Create stream (serially, thread-safe)
            std::shared_ptr<IQuicStream> stream;
            for (int retry = 0; retry < 300; retry++) {
                stream = conn->MakeStream(StreamDirection::kBidi);
                if (stream) break;
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            }
            if (!stream) {
                std::cerr << "Failed to create stream for " << url << " after retries" << std::endl;
                state->completed++;
                continue;
            }
            
            auto bidi = std::dynamic_pointer_cast<IQuicBidirectionStream>(stream);
            if (!bidi) {
                std::cerr << "Failed to cast stream for " << url << std::endl;
                state->completed++;
                continue;
            }
            
            auto ctx = std::make_shared<DownloadContext>();
            ctx->filepath = filepath;
            ctx->file = fopen(filepath.c_str(), "wb");
            if (!ctx->file) {
                std::cerr << "Failed to open file: " << filepath << std::endl;
                state->completed++;
                continue;
            }
            
            bidi->SetStreamReadCallBack(
                [ctx, state](std::shared_ptr<IBufferRead> data, bool is_last, uint32_t error) {
                    if (error != 0) {
                        std::lock_guard<std::mutex> lock(ctx->mtx);
                        if (ctx->file) { fclose(ctx->file); ctx->file = nullptr; }
                        ctx->done = true;
                        state->completed++;
                        state->cv.notify_all();
                        return;
                    }
                    
                    if (data) {
                        uint32_t len = data->GetDataLength();
                        if (len > 0) {
                            std::vector<uint8_t> buf(len);
                            uint32_t read = data->Read(buf.data(), len);
                            if (ctx->file && read > 0) {
                                fwrite(buf.data(), 1, read, ctx->file);
                                ctx->bytes_received += read;
                            }
                        }
                    }
                    
                    if (is_last) {
                        if (ctx->file) { fclose(ctx->file); ctx->file = nullptr; }
                        std::cout << "Downloaded " << ctx->bytes_received << " bytes -> "
                                  << ctx->filepath << std::endl;
                        ctx->success = true;
                        ctx->done = true;
                        state->succeeded++;
                        state->completed++;
                        state->cv.notify_all();
                    }
                });
            
            std::string request = "GET " + path + "\r\n";
            bidi->Send(reinterpret_cast<uint8_t*>(request.data()),
                       static_cast<uint32_t>(request.size()));
            bidi->Flush();
            // Close send direction (FIN) - server waits for FIN before responding
            bidi->Close();
            std::cout << "Sent request: GET " << path << std::endl;
            initiated++;
        }
        
        // Wait for all downloads to complete (or timeout)
        {
            std::unique_lock<std::mutex> lock(state->mtx);
            state->cv.wait_for(lock, std::chrono::seconds(60),
                [&state] { return state->completed.load() >= state->total; });
        }
        
        std::cout << "All " << state->succeeded.load() << "/" << state->total
                  << " downloads completed (" << initiated << " initiated)" << std::endl;
        
        return state->succeeded.load() == state->total;
    }

    bool InitiateMigration() {
        std::lock_guard<std::mutex> lock(conn_mtx_);
        if (!conn_) {
            return false;
        }
        return conn_->InitiateMigration();
    }

    void Shutdown() {
        if (shutdown_done_) return;
        shutdown_done_ = true;
        {
            std::lock_guard<std::mutex> lock(conn_mtx_);
            if (conn_) {
                conn_->Close();
                conn_.reset();
            }
        }
        if (quic_) {
            quic_->AddTimer(500, [this]() { 
                if (quic_) {
                    quic_->Destroy(); 
                }
            });
            quic_->Join();
            quic_.reset();
        }
    }

private:
    void OnConnection(std::shared_ptr<IQuicConnection> conn, ConnectionOperation op,
                      uint32_t error, const std::string& reason) {
        if (op == ConnectionOperation::kConnectionClose) {
            std::cout << "Connection closed, error=" << error << " reason=" << reason << std::endl;
            std::lock_guard<std::mutex> lock(conn_mtx_);
            conn_.reset();
            conn_failed_ = true;
            conn_cv_.notify_all();
            return;
        }

        // kEarlyConnection (0-RTT ready) or kConnectionCreate (handshake done)
        // Both signal that the connection is usable for sending data
        if (op == ConnectionOperation::kEarlyConnection) {
            std::cout << "0-RTT early connection ready" << std::endl;
        } else {
            std::string addr;
            uint32_t port;
            conn->GetRemoteAddr(addr, port);
            std::cout << "Connected to " << addr << ":" << port << std::endl;
        }

        {
            std::lock_guard<std::mutex> lock(conn_mtx_);
            if (!conn_ready_) {
                conn_ = conn;
                conn_ready_ = true;
            }
        }
        conn_cv_.notify_all();
    }

    std::string ExtractPath(const std::string& url) {
        // "https://server:port/path" -> "/path"
        auto scheme_end = url.find("://");
        if (scheme_end == std::string::npos) {
            return url.empty() || url[0] != '/' ? "/" + url : url;
        }
        auto path_start = url.find('/', scheme_end + 3);
        if (path_start == std::string::npos) {
            return "/";
        }
        return url.substr(path_start);
    }

    std::string ExtractFilename(const std::string& url) {
        size_t last_slash = url.find_last_of('/');
        if (last_slash != std::string::npos && last_slash + 1 < url.length()) {
            return url.substr(last_slash + 1);
        }
        return "download.bin";
    }

    std::shared_ptr<IQuicClient> quic_;
    std::string download_dir_;
    std::string qlog_dir_;

    std::shared_ptr<IQuicConnection> conn_;
    std::mutex conn_mtx_;
    std::condition_variable conn_cv_;
    bool conn_ready_ = false;
    bool conn_failed_ = false;
    bool shutdown_done_ = false;
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
    // Support both environment variables and command-line arguments
    std::string server;
    uint16_t port = 443;
    std::string downloads_dir = "/downloads";
    std::string qlog_dir;
    std::vector<std::string> urls;
    bool expect_retry = false;
    bool enable_zerortt = false;
    bool enable_resumption = false;
    bool force_keyupdate = false;
    bool enable_http3 = false;
    std::string session_cache;
    std::string cipher_suite;
    uint32_t force_version = 0;
    uint32_t quic_version = 0;
    
    // Parse command-line arguments
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--server" && i + 1 < argc) {
            server = argv[++i];
        } else if (arg == "--port" && i + 1 < argc) {
            port = std::atoi(argv[++i]);
        } else if (arg == "--download-dir" && i + 1 < argc) {
            downloads_dir = argv[++i];
        } else if (arg == "--qlog-dir" && i + 1 < argc) {
            qlog_dir = argv[++i];
        } else if (arg == "--expect-retry") {
            expect_retry = true;
        } else if (arg == "--zerortt") {
            enable_zerortt = true;
        } else if (arg == "--resumption") {
            enable_resumption = true;
        } else if (arg == "--force-keyupdate") {
            force_keyupdate = true;
        } else if (arg == "--http3") {
            enable_http3 = true;
        } else if (arg == "--session-cache" && i + 1 < argc) {
            session_cache = argv[++i];
        } else if (arg == "--cipher" && i + 1 < argc) {
            cipher_suite = argv[++i];
        } else if (arg == "--force-version" && i + 1 < argc) {
            force_version = static_cast<uint32_t>(std::strtoul(argv[++i], nullptr, 0));
        } else if (arg == "--quic-version" && i + 1 < argc) {
            quic_version = static_cast<uint32_t>(std::strtoul(argv[++i], nullptr, 0));
        } else if (arg.rfind("https://", 0) == 0 || arg.rfind("http://", 0) == 0) {
            // URL argument
            urls.push_back(arg);
        }
    }
    
    // Fall back to environment variables if not provided via command line
    const char* server_env = std::getenv("SERVER");
    const char* port_env = std::getenv("PORT");
    const char* requests_env = std::getenv("REQUESTS");
    const char* downloads_env = std::getenv("DOWNLOADS");
    const char* testcase_env = std::getenv("TESTCASE");
    const char* qlog_env = std::getenv("QLOGDIR");
    
    if (server.empty() && server_env) {
        server = server_env;
    }
    if (port == 443 && port_env) {
        port = std::atoi(port_env);
    }
    if (downloads_dir == "/downloads" && downloads_env) {
        downloads_dir = downloads_env;
    }
    if (qlog_dir.empty() && qlog_env) {
        qlog_dir = qlog_env;
    }
    if (urls.empty() && requests_env) {
        urls = ParseUrls(requests_env);
    }
    
    // If server not specified, extract from first URL
    if (server.empty() && !urls.empty()) {
        // Parse first URL to extract server and port
        // URL format: https://hostname:port/path
        std::string first_url = urls[0];
        size_t proto_end = first_url.find("://");
        if (proto_end != std::string::npos) {
            size_t host_start = proto_end + 3;
            size_t host_end = first_url.find("/", host_start);
            if (host_end == std::string::npos) {
                host_end = first_url.length();
            }
            std::string host_port = first_url.substr(host_start, host_end - host_start);
            
            // Check if port is specified
            size_t colon_pos = host_port.find(":");
            if (colon_pos != std::string::npos) {
                server = host_port.substr(0, colon_pos);
                port = std::atoi(host_port.substr(colon_pos + 1).c_str());
            } else {
                server = host_port;
            }
            std::cout << "Extracted server from URL: " << server << ":" << port << std::endl;
        }
    }
    
    // Validate required parameters
    if (server.empty() || urls.empty()) {
        std::cerr << "Error: SERVER and URLs are required" << std::endl;
        std::cerr << "Usage: " << argv[0] << " --server <host> [--port <port>] [--download-dir <dir>] [--qlog-dir <dir>] <url1> [url2 ...]" << std::endl;
        std::cerr << "   Or set SERVER and REQUESTS environment variables, or provide URLs (server will be extracted from URL)" << std::endl;
        return 1;
    }

    // Auto-detect HTTP/3 mode from TESTCASE environment variable
    const char* testcase_detect = std::getenv("TESTCASE");
    if (!enable_http3 && testcase_detect && strcmp(testcase_detect, "http3") == 0) {
        enable_http3 = true;
    }

    // Set environment variables from command-line arguments for HqInteropClient::Init()
    if (enable_zerortt) {
        setenv("ZERORTT", "1", 1);
    }
    if (enable_resumption || enable_zerortt) {
        if (!session_cache.empty()) {
            setenv("SESSION_FILE", session_cache.c_str(), 1);
        }
    }
    if (force_keyupdate) {
        setenv("KEY_UPDATE", "1", 1);
    }
    if (!cipher_suite.empty()) {
        setenv("CIPHER_SUITE", cipher_suite.c_str(), 1);
    }
    if (quic_version > 0) {
        char version_buf[32];
        snprintf(version_buf, sizeof(version_buf), "0x%08x", quic_version);
        setenv("QUIC_VERSION", version_buf, 1);
    }
    
    bool is_migration_test = testcase_env && (strcmp(testcase_env, "connectionmigration") == 0);

    std::cout << "========================================" << std::endl;
    std::cout << "quicX hq-interop Client" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Server: " << server << ":" << port << std::endl;
    std::cout << "Downloads: " << downloads_dir << std::endl;
    std::cout << "URLs: " << urls.size() << std::endl;
    for (const auto& url : urls) {
        std::cout << "  - " << url << std::endl;
    }
    if (testcase_env) {
        std::cout << "TESTCASE: " << testcase_env << std::endl;
    }
    if (is_migration_test) {
        std::cout << "*** Connection Migration Test ***" << std::endl;
    }
    if (expect_retry) {
        std::cout << "*** Expecting Retry ***" << std::endl;
    }
    if (enable_http3) {
        std::cout << "*** HTTP/3 Mode (ALPN=h3) ***" << std::endl;
        std::cout << "========================================" << std::endl;
        std::cout << std::endl;

        // Use HTTP/3 client (IClient with h3 ALPN)
        auto h3_client = IClient::Create();

        Http3ClientConfig h3_config;
        h3_config.quic_config_.verify_peer_ = false;  // Self-signed certs
        h3_config.quic_config_.config_.worker_thread_num_ = 4;
        h3_config.quic_config_.config_.log_level_ = LogLevel::kDebug;
        h3_config.quic_config_.config_.log_path_ = "./logs";
        h3_config.connection_timeout_ms_ = 30000;
        // Use QUIC v1 for interop compatibility (quic-go interop image may not support v2)
        h3_config.quic_config_.config_.quic_version_ = 0x00000001;

        // QLog
        if (!qlog_dir.empty()) {
            h3_config.quic_config_.config_.qlog_config_.enabled = true;
            h3_config.quic_config_.config_.qlog_config_.output_dir = qlog_dir;
        }
        // SSLKEYLOG
        const char* h3_keylog = std::getenv("SSLKEYLOGFILE");
        if (h3_keylog) {
            h3_config.quic_config_.config_.keylog_file_ = h3_keylog;
        }

        if (!h3_client->Init(h3_config)) {
            std::cerr << "Failed to initialize HTTP/3 client" << std::endl;
            return 1;
        }

        // Download all files using HTTP/3 (complete mode with async handler for streaming)
        std::atomic<int> completed{0};
        std::atomic<int> succeeded{0};
        std::mutex io_mtx;
        std::condition_variable done_cv;
        int total = static_cast<int>(urls.size());

        for (const auto& url : urls) {
            // Extract filename from URL for saving
            std::string filename = "download.bin";
            size_t last_slash = url.find_last_of('/');
            if (last_slash != std::string::npos && last_slash + 1 < url.length()) {
                filename = url.substr(last_slash + 1);
            }
            std::string filepath = downloads_dir + "/" + filename;

            std::cout << "H3 Downloading: " << url << " -> " << filepath << std::endl;

            auto req = IRequest::Create();

            // Create async handler for streaming file download
            class FileDownloadHandler : public IAsyncClientHandler {
            public:
                FileDownloadHandler(const std::string& filepath, const std::string& url,
                                    std::atomic<int>& completed, std::atomic<int>& succeeded,
                                    std::mutex& io_mtx, std::condition_variable& done_cv)
                    : filepath_(filepath), url_(url), completed_(completed), succeeded_(succeeded),
                      io_mtx_(io_mtx), done_cv_(done_cv) {}

                void OnHeaders(std::shared_ptr<IResponse> response) override {
                    status_code_ = response->GetStatusCode();
                    if (status_code_ == 200) {
                        file_ = fopen(filepath_.c_str(), "wb");
                        if (!file_) {
                            std::cerr << "H3 Failed to open file: " << filepath_ << std::endl;
                        }
                    } else {
                        std::cerr << "H3 HTTP error " << status_code_ << " for " << url_ << std::endl;
                    }
                }

                void OnBodyChunk(const uint8_t* data, size_t length, bool is_last) override {
                    if (file_ && length > 0) {
                        fwrite(data, 1, length, file_);
                        bytes_received_ += length;
                    }
                    if (is_last) {
                        if (file_) {
                            fclose(file_);
                            file_ = nullptr;
                        }
                        std::cout << "H3 Downloaded " << bytes_received_ << " bytes -> "
                                  << filepath_ << " (status=" << status_code_ << ")" << std::endl;
                        if (status_code_ == 200 && bytes_received_ > 0) {
                            succeeded_++;
                        }
                        completed_++;
                        done_cv_.notify_all();
                    }
                }

                void OnError(uint32_t error_code) override {
                    std::cerr << "H3 Protocol error " << error_code << " for " << url_ << std::endl;
                    if (file_) {
                        fclose(file_);
                        file_ = nullptr;
                    }
                    completed_++;
                    done_cv_.notify_all();
                }

            private:
                std::string filepath_;
                std::string url_;
                std::atomic<int>& completed_;
                std::atomic<int>& succeeded_;
                std::mutex& io_mtx_;
                std::condition_variable& done_cv_;
                FILE* file_ = nullptr;
                size_t bytes_received_ = 0;
                uint32_t status_code_ = 0;
            };

            auto handler = std::make_shared<FileDownloadHandler>(
                filepath, url, completed, succeeded, io_mtx, done_cv);

            if (!h3_client->DoRequest(url, HttpMethod::kGet, req, handler)) {
                std::cerr << "H3 Failed to send request for " << url << std::endl;
                completed++;
            }
        }

        // Wait for all downloads to complete
        {
            std::unique_lock<std::mutex> lock(io_mtx);
            done_cv.wait_for(lock, std::chrono::seconds(60),
                [&] { return completed.load() >= total; });
        }

        std::cout << "H3 All " << succeeded.load() << "/" << total
                  << " downloads completed" << std::endl;

        h3_client->Close();

        if (succeeded.load() != total) {
            return 1;
        }
        std::cout << "Client finished successfully (HTTP/3)" << std::endl;
        return 0;
    }
    std::cout << "========================================" << std::endl;
    std::cout << std::endl;

    HqInteropClient client(downloads_dir, qlog_dir);

    if (!client.Init()) {
        return 1;
    }

    // Resumption / ZeroRTT: need TWO connections
    // 1st connection: normal handshake, download some files, save session
    // 2nd connection: use saved session (resumption/0-RTT), download remaining files
    if ((enable_resumption || enable_zerortt) && urls.size() >= 2) {
        std::cout << "*** Two-connection test (" << (enable_zerortt ? "0-RTT" : "Resumption") << ") ***" << std::endl;
        
        // For 0-RTT: minimize 1-RTT data by downloading only 1 file in connection 1
        // The test checks that 1-RTT payload < 50% of total filename data.
        // For Resumption: split evenly (no such constraint).
        size_t split = enable_zerortt ? 1 : urls.size() / 2;
        std::vector<std::string> urls_first(urls.begin(), urls.begin() + split);
        std::vector<std::string> urls_second(urls.begin() + split, urls.end());
        
        // --- Connection 1: normal handshake ---
        std::cout << "--- Connection 1: normal handshake (" << urls_first.size() << " files) ---" << std::endl;
        if (!client.Connect(server, port)) {
            return 1;
        }
        if (!client.DownloadAll(urls_first)) {
            client.Shutdown();
            return 1;
        }
        // Give time for NewSessionTicket to arrive before closing
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        client.Shutdown();
        std::cout << "Connection 1 closed, session saved" << std::endl;
        
        // Small delay between connections
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        
        // --- Connection 2: resumption/0-RTT ---
        std::cout << "--- Connection 2: " << (enable_zerortt ? "0-RTT" : "Resumption") << " (" << urls_second.size() << " files) ---" << std::endl;
        
        // Reinitialize client for second connection
        HqInteropClient client2(downloads_dir, qlog_dir);
        if (!client2.Init()) {
            return 1;
        }
        if (!client2.Connect(server, port)) {
            return 1;
        }
        if (!client2.DownloadAll(urls_second)) {
            client2.Shutdown();
            return 1;
        }
        client2.Shutdown();
        std::cout << "Client finished successfully (two connections)" << std::endl;
        return 0;
    }

    if (!client.Connect(server, port)) {
        client.Shutdown();
        return 1;
    }

    // For connection migration test, trigger migration during first download
    if (is_migration_test && !urls.empty()) {
        std::thread migration_thread([&client]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            std::cout << "ConnectionMigration: triggering migration..." << std::endl;
            if (client.InitiateMigration()) {
                std::cout << "ConnectionMigration: migration initiated successfully" << std::endl;
            } else {
                std::cerr << "ConnectionMigration: migration initiation failed" << std::endl;
            }
        });
        migration_thread.detach();
    }

    if (!client.DownloadAll(urls)) {
        client.Shutdown();
        return 1;
    }

    client.Shutdown();
    std::cout << "Client finished successfully" << std::endl;
    return 0;
}
