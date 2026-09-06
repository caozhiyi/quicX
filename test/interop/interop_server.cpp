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
 *   QUIC_VERSION   - QUIC version hex (e.g. 0x00000001 for v1, 0x6b3343cf for v2)
 */

#include <cctype>
#include <cstdlib>
#include <cstring>
#ifdef _WIN32
#define setenv(name, value, overwrite) _putenv_s(name, value)
#else
#include <execinfo.h>
#include <unistd.h>
#endif
#include <iostream>
#include <memory>
#include <quicx/http3/if_request.h>
#include <quicx/http3/if_response.h>
#include <quicx/http3/if_server.h>
#include <quicx/quic/if_quic_bidirection_stream.h>
#include <quicx/quic/if_quic_connection.h>
#include <quicx/quic/if_quic_server.h>
#include <signal.h>
#include <string>

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
    HqInteropServer(const std::string& root_dir, uint16_t port, const std::string& preferred_address_v4 = "",
        const std::string& preferred_address_v6 = ""):
        root_dir_(root_dir),
        port_(port),
        preferred_address_v4_(preferred_address_v4),
        preferred_address_v6_(preferred_address_v6) {}

    bool Init(const std::string& cert_file, const std::string& key_file) {
        QuicTransportParams transport_params;
        // Use a short idle timeout for interop testing so the connection closes
        // promptly after all streams are finished, well within container timeout.
        transport_params.max_idle_timeout_ms_ = 10000;  // 10 seconds
        // RFC 9000 §9.6: advertise the server's alternate address so the client
        // migrates to it (connectionmigration interop scenario).
        transport_params.preferred_address_v4_ = preferred_address_v4_;
        transport_params.preferred_address_v6_ = preferred_address_v6_;
        if (!preferred_address_v4_.empty() || !preferred_address_v6_.empty()) {
            std::cout << "Advertising preferred address: v4="
                      << (preferred_address_v4_.empty() ? "-" : preferred_address_v4_)
                      << " v6=" << (preferred_address_v6_.empty() ? "-" : preferred_address_v6_) << std::endl;
        }
        quic_ = IQuicServer::Create(transport_params);

        quic_->SetConnectionStateCallBack(
            [this](std::shared_ptr<IQuicConnection> conn, ConnectionOperation op, uint32_t error,
                const std::string& reason) { OnConnection(conn, op, error, reason); });

        QuicServerConfig config;
        config.cert_file_ = cert_file;
        config.key_file_ = key_file;
        config.alpn_ = kHqInteropAlpn;
        config.config_.worker_thread_num_ = 4;
        // Default to kError to avoid gigabyte-sized debug logs across repeated
        // interop runs (which also slow large-file scenarios enough to hit the
        // per-download 30s timeout). LOG_LEVEL env var overrides (debug/info/
        // warn/error/fatal/null).
        config.config_.log_level_ = LogLevel::kError;
        if (const char* lvl = std::getenv("LOG_LEVEL")) {
            std::string s = lvl;
            for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (s == "debug")
                config.config_.log_level_ = LogLevel::kDebug;
            else if (s == "info")
                config.config_.log_level_ = LogLevel::kInfo;
            else if (s == "warn" || s == "warning")
                config.config_.log_level_ = LogLevel::kWarn;
            else if (s == "error")
                config.config_.log_level_ = LogLevel::kError;
            else if (s == "fatal")
                config.config_.log_level_ = LogLevel::kFatal;
            else if (s == "null" || s == "off" || s == "none")
                config.config_.log_level_ = LogLevel::kNull;
        }
        config.config_.log_path_ = "./logs";  // Current directory for logs

        // QLog
        const char* qlog_dir = std::getenv("QLOGDIR");
        if (qlog_dir) {
            config.config_.qlog_config_.enabled_ = true;
            config.config_.qlog_config_.output_dir_ = qlog_dir;
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

        // Key Update (RFC 9001 §6) — set by --enable-keyupdate (run_endpoint.sh).
        // The server sends the bulk of the transfer bytes, so it is the side that
        // actually reaches the send-byte threshold and initiates the key update;
        // the client then responds with its own key-phase-1 packets. Without this,
        // neither peer sends key-phase-1 packets and the keyupdate test fails.
        const char* keyupdate = std::getenv("ENABLE_KEYUPDATE");
        if (keyupdate && std::atoi(keyupdate) == 1) {
            config.config_.enable_key_update_ = true;
            std::cout << "Key Update enabled" << std::endl;
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
        // The advertised preferred address must actually be listened on, or
        // the client's PATH_CHALLENGE probes get no response and migration
        // stalls. Add a second listener on the advertised port (picoquic-style
        // "443:4433"); datagrams are dispatched by DCID, so both listeners
        // feed the same connections.
        uint16_t pref_port = GetPreferredPort();
        if (pref_port != 0 && pref_port != port_) {
            if (!quic_->ListenAndAccept("::", pref_port)) {
                std::cerr << "Failed to start listening on preferred port " << pref_port << std::endl;
                return false;
            }
            std::cout << "Server listening on [::]:" << pref_port << " (preferred address)" << std::endl;
        }
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
    void OnConnection(
        std::shared_ptr<IQuicConnection> conn, ConnectionOperation op, uint32_t error, const std::string& reason) {
        if (op == ConnectionOperation::kConnectionClose) {
            std::string addr;
            uint32_t port;
            conn->GetRemoteAddr(addr, port);
            std::cout << "Connection closed from " << addr << ":" << port << " error=" << error << " reason=" << reason
                      << std::endl;
            return;
        }

        // New connection
        std::string addr;
        uint32_t port;
        conn->GetRemoteAddr(addr, port);
        std::cout << "New connection from " << addr << ":" << port << std::endl;

        conn->SetStreamStateCallBack(
            [this](std::shared_ptr<IQuicStream> stream, uint32_t err) { OnStream(stream, err); });
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

        bidi->SetStreamReadCallBack([ctx](std::shared_ptr<IBufferRead> data, bool is_last, uint32_t err) {
            OnStreamData(ctx, data, is_last, err);
        });
    }

    static void OnStreamData(
        std::shared_ptr<StreamContext> ctx, std::shared_ptr<IBufferRead> data, bool is_last, uint32_t error) {
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
    std::string preferred_address_v4_;
    std::string preferred_address_v6_;

    // Extract the port from the first non-empty preferred address string
    // ("<ipv4>:<port>" or "[<ipv6>]:<port>"). Returns 0 if none is set.
    uint16_t GetPreferredPort() const {
        const std::string& addr = !preferred_address_v4_.empty() ? preferred_address_v4_ : preferred_address_v6_;
        auto pos = addr.rfind(':');
        if (pos == std::string::npos) {
            return 0;
        }
        int port = std::atoi(addr.c_str() + pos + 1);
        return (port > 0 && port <= 65535) ? static_cast<uint16_t>(port) : 0;
    }
};

// =============================================================================
// HTTP/3 Interop Server (ALPN=h3, standard HTTP/3 protocol)
// =============================================================================

class H3InteropServer {
public:
    H3InteropServer(const std::string& root_dir, uint16_t port):
        root_dir_(root_dir),
        port_(port) {}

    bool Init(const std::string& cert_file, const std::string& key_file) {
        server_ = IServer::Create();

        Http3ServerConfig config;
        config.quic_config_.cert_file_ = cert_file;
        config.quic_config_.key_file_ = key_file;
        config.quic_config_.config_.worker_thread_num_ = 4;
        config.quic_config_.config_.log_level_ = LogLevel::kDebug;
        config.quic_config_.config_.log_path_ = "./logs";

        // QLog
        const char* qlog_dir = std::getenv("QLOGDIR");
        if (qlog_dir) {
            config.quic_config_.config_.qlog_config_.enabled_ = true;
            config.quic_config_.config_.qlog_config_.output_dir_ = qlog_dir;
            std::cout << "QLog enabled, output: " << qlog_dir << std::endl;
        }

        // SSLKEYLOG
        const char* keylog = std::getenv("SSLKEYLOGFILE");
        if (keylog) {
            config.quic_config_.config_.keylog_file_ = keylog;
            std::cout << "SSLKEYLOG enabled: " << keylog << std::endl;
        }

        // QUIC Version
        const char* quic_version = std::getenv("QUIC_VERSION");
        if (quic_version) {
            uint32_t version = static_cast<uint32_t>(std::strtoul(quic_version, nullptr, 0));
            config.quic_config_.config_.quic_version_ = version;
            std::cout << "QUIC Version: 0x" << std::hex << version << std::dec << std::endl;
        } else {
            // Default to QUIC v1 for interop compatibility
            config.quic_config_.config_.quic_version_ = 0x00000001;
        }

        if (!server_->Init(config)) {
            std::cerr << "Failed to initialize HTTP/3 server" << std::endl;
            return false;
        }

        // Register wildcard GET handler to serve any file from www root
        std::string root = root_dir_;
        server_->AddHandler(
            HttpMethod::kGet, "/*filepath", [root](std::shared_ptr<IRequest> req, std::shared_ptr<IResponse> resp) {
                std::string path = req->GetPath();
                if (path.empty() || path[0] != '/') {
                    path = "/" + path;
                }

                std::string filepath = root + path;
                std::cout << "H3 Serving: " << filepath << std::endl;

                FILE* file = fopen(filepath.c_str(), "rb");
                if (!file) {
                    std::cerr << "H3 File not found: " << filepath << std::endl;
                    resp->SetStatusCode(404);
                    resp->AppendBody("Not Found");
                    return;
                }

                // Get file size
                fseek(file, 0, SEEK_END);
                long file_size = ftell(file);
                fseek(file, 0, SEEK_SET);
                std::cout << "H3 File size: " << file_size << " bytes" << std::endl;

                resp->SetStatusCode(200);
                resp->AddHeader("content-type", "application/octet-stream");
                resp->AddHeader("content-length", std::to_string(file_size));

                // Use body provider for streaming large files
                // Wrap FILE* in shared_ptr to safely track closure state
                struct FileState {
                    FILE* fp;
                    FileState(FILE* f):
                        fp(f) {}
                    ~FileState() {
                        if (fp) fclose(fp);
                    }
                };
                auto state = std::make_shared<FileState>(file);
                resp->SetResponseBodyProvider([state](uint8_t* buf, size_t size) -> size_t {
                    if (!state->fp) {
                        return 0;  // Already closed
                    }
                    size_t read = fread(buf, 1, size, state->fp);
                    if (read == 0) {
                        fclose(state->fp);
                        state->fp = nullptr;
                    }
                    return read;
                });
            });

        std::cout << "HTTP/3 Server initialized on port " << port_ << std::endl;
        std::cout << "Serving files from: " << root_dir_ << std::endl;
        std::cout << "ALPN: h3" << std::endl;
        return true;
    }

    bool Start() {
        if (!server_->Start("::", port_)) {
            std::cerr << "Failed to start HTTP/3 server" << std::endl;
            return false;
        }
        std::cout << "HTTP/3 Server listening on [::]:" << port_ << std::endl;
        server_->Join();
        return true;
    }

    void Stop() {
        if (server_) {
            server_->Stop();
        }
    }

private:
    std::shared_ptr<IServer> server_;
    std::string root_dir_;
    uint16_t port_;
};

static HqInteropServer* g_server = nullptr;

static H3InteropServer* g_h3_server = nullptr;

void signal_handler(int signum) {
    std::cout << "\nReceived signal " << signum << ", shutting down..." << std::endl;
    if (g_server) {
        g_server->Stop();
    }
    if (g_h3_server) {
        g_h3_server->Stop();
    }
}

#ifndef _WIN32
// Async-signal-safe crash dumper: prints the faulting thread's backtrace to
// stderr so interop failures that end in SIGSEGV/SIGABRT leave a diagnosable
// trace in the container log instead of a bare "Exited (139)".
void crash_handler(int signum) {
    static const char kMsg[] = "\n*** interop_server fatal signal ";
    ssize_t ignored = write(STDERR_FILENO, kMsg, sizeof(kMsg) - 1);
    char num[8];
    int n = 0;
    int v = signum;
    if (v == 0) {
        num[n++] = '0';
    }
    while (v > 0 && n < 7) {
        num[n++] = static_cast<char>('0' + (v % 10));
        v /= 10;
    }
    for (int i = n - 1; i >= 0; --i) {
        ignored = write(STDERR_FILENO, &num[i], 1);
    }
    ignored = write(STDERR_FILENO, " ***\n", 5);

    void* frames[64];
    int count = backtrace(frames, 64);
    backtrace_symbols_fd(frames, count, STDERR_FILENO);
    (void)ignored;

    signal(signum, SIG_DFL);
    raise(signum);
}
#endif

int main(int argc, char* argv[]) {
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
#ifndef _WIN32
    signal(SIGSEGV, crash_handler);
    signal(SIGABRT, crash_handler);
    signal(SIGBUS, crash_handler);
    signal(SIGFPE, crash_handler);
#endif

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
    bool enable_http3 = false;
    std::string cipher_suite;
    uint32_t quic_version = 0;
    bool strict_version = false;
    std::string preferred_address_v4;
    std::string preferred_address_v6;

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
        } else if (arg == "--http3") {
            enable_http3 = true;
        } else if (arg == "--cipher" && i + 1 < argc) {
            cipher_suite = argv[++i];
        } else if (arg == "--quic-version" && i + 1 < argc) {
            quic_version = static_cast<uint32_t>(std::strtoul(argv[++i], nullptr, 0));
        } else if (arg == "--preferred-address-v4" && i + 1 < argc) {
            preferred_address_v4 = argv[++i];
        } else if (arg == "--preferred-address-v6" && i + 1 < argc) {
            preferred_address_v6 = argv[++i];
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

    // Auto-detect HTTP/3 mode from TESTCASE environment variable
    const char* testcase_env = std::getenv("TESTCASE");
    if (!enable_http3 && testcase_env && strcmp(testcase_env, "http3") == 0) {
        enable_http3 = true;
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
    std::cout << (enable_http3 ? "quicX HTTP/3 Interop Server" : "quicX hq-interop Server") << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Port: " << port << std::endl;
    std::cout << "WWW: " << www_dir << std::endl;
    std::cout << "Mode: " << (enable_http3 ? "HTTP/3 (ALPN=h3)" : "hq-interop") << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << std::endl;

    std::cout << "Certificate: " << cert_file << std::endl;
    std::cout << "Private Key: " << key_file << std::endl;

    if (enable_http3) {
        // HTTP/3 mode: use IServer with h3 ALPN
        H3InteropServer h3_server(www_dir, port);
        g_h3_server = &h3_server;

        if (!h3_server.Init(cert_file, key_file)) {
            return 1;
        }

        if (!h3_server.Start()) {
            return 1;
        }

        std::cout << "HTTP/3 Server stopped" << std::endl;
    } else {
        // hq-interop mode: use HqInteropServer
        HqInteropServer server(www_dir, port, preferred_address_v4, preferred_address_v6);
        g_server = &server;

        if (!server.Init(cert_file, key_file)) {
            return 1;
        }

        if (!server.Start()) {
            return 1;
        }

        std::cout << "Server stopped" << std::endl;
    }
    return 0;
}
