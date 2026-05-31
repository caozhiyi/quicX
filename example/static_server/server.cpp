// Static file HTTP/3 server example, with built-in TCP upgrade endpoint.
//
// What this single process does:
//   * Starts an HTTP/3 server on UDP `h3_port` (default 7010) that actually
//     serves files from a document root.
//   * Starts the quicX `upgrade` module on TCP `http_port` / `https_port`
//     (default 8080 / 8443). The upgrade module speaks just enough HTTP/1.1
//     and HTTP/2 to advertise `Alt-Svc: h3=":<h3_port>"` so Chrome will jump
//     to HTTP/3 on subsequent requests.
//
// Why both? Chrome (and most browsers) do NOT speak naked HTTP/3. A browser
// always opens TCP+TLS first, looks at `Alt-Svc`, and only then upgrades to
// HTTP/3. Running the upgrade endpoint in the same process means a user can
// hit `https://<host>:8443/` once, and from then on Chrome will use H3 for
// the same origin automatically. (You can still bypass this with the Chrome
// flag `--origin-to-force-quic-on=<host>:<h3_port>`.)
//
// Caveat: the upgrade module does NOT itself serve file content -- on H1 it
// just returns a small stub body that says "h3 available on :<port>". So the
// real static-file serving still happens over HTTP/3.
//
// Usage:
//   static_server [options]
// Options:
//   --doc-root <dir>     Document root (default ./www)
//   --cert <file>        TLS cert PEM file (default ./cert.pem)
//   --key  <file>        TLS key  PEM file (default ./key.pem)
//   --host <addr>        Bind address (default 0.0.0.0)
//   --h3-port <port>     UDP port for HTTP/3 (default 7010)
//   --http-port <port>   TCP port for HTTP/1.1 plaintext (default 8080,
//                        set 0 to disable)
//   --https-port <port>  TCP port for HTTPS / Alt-Svc (default 8443,
//                        set 0 to disable)
//   --no-upgrade         Do not start the TCP upgrade endpoint at all.

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>

#include <quicx/http3/if_request.h>
#include <quicx/http3/if_response.h>
#include <quicx/http3/if_server.h>

#include <quicx/common/if_event_loop.h>
#include <quicx/upgrade/if_upgrade.h>
#include <quicx/upgrade/type.h>

// Internal headers -- we use the same StdoutLogger that the upgrade_h3
// example uses so that LOG_INFO/LOG_ERROR from the upgrade module
// (TLS handshake debug, AddListener failure reasons, ALPN selection,
// "[TLSDBG] ...", etc.) actually reach the terminal. Without this the
// upgrade module's diagnostics are silently dropped (BaseLogger has no
// default sink) and any failure looks like "curl just hangs" with zero
// information on the server side.
#include "common/log/log.h"
#include "common/log/stdout_logger.h"

namespace fs = std::filesystem;

static std::atomic<bool> g_shutdown{false};
static void HandleSignal(int) { g_shutdown.store(true, std::memory_order_release); }

// ---- helpers ---------------------------------------------------------------

// Map file extension -> Content-Type
static std::string GuessContentType(const fs::path& p) {
    static const std::unordered_map<std::string, std::string> kMime = {
        {".html", "text/html; charset=utf-8"},
        {".htm",  "text/html; charset=utf-8"},
        {".css",  "text/css; charset=utf-8"},
        {".js",   "application/javascript; charset=utf-8"},
        {".mjs",  "application/javascript; charset=utf-8"},
        {".json", "application/json; charset=utf-8"},
        {".txt",  "text/plain; charset=utf-8"},
        {".md",   "text/plain; charset=utf-8"},
        {".png",  "image/png"},
        {".jpg",  "image/jpeg"},
        {".jpeg", "image/jpeg"},
        {".gif",  "image/gif"},
        {".svg",  "image/svg+xml"},
        {".webp", "image/webp"},
        {".ico",  "image/x-icon"},
        {".woff", "font/woff"},
        {".woff2","font/woff2"},
        {".wasm", "application/wasm"},
    };
    auto it = kMime.find(p.extension().string());
    return it != kMime.end() ? it->second : "application/octet-stream";
}

// Resolve request path against the document root, with directory-traversal
// protection. Returns empty path on rejection.
static fs::path ResolveSafePath(const fs::path& root, std::string url_path) {
    auto qpos = url_path.find('?');
    if (qpos != std::string::npos) url_path.resize(qpos);

    if (url_path.empty() || url_path.front() != '/') return {};
    std::string rel = url_path.substr(1);
    if (rel.empty()) rel = "index.html";

    fs::path full = root / rel;
    fs::path canonical_root;
    fs::path canonical_full;
    try {
        canonical_root = fs::canonical(root);
        canonical_full = fs::weakly_canonical(full);
    } catch (...) {
        return {};
    }
    auto rs = canonical_root.string();
    auto fs_ = canonical_full.string();
    if (fs_.compare(0, rs.size(), rs) != 0) return {};
    return canonical_full;
}

static void ServeFile(const fs::path& doc_root,
                      uint16_t h3_port,
                      std::shared_ptr<quicx::IRequest>  req,
                      std::shared_ptr<quicx::IResponse> resp) {
    std::string url_path = req->GetPath();
    fs::path file = ResolveSafePath(doc_root, url_path);

    if (file.empty()) {
        resp->SetStatusCode(403);
        resp->AddHeader("Content-Type", "text/plain; charset=utf-8");
        resp->AppendBody("403 Forbidden");
        return;
    }
    if (fs::is_directory(file)) {
        file /= "index.html";
    }
    if (!fs::exists(file) || !fs::is_regular_file(file)) {
        resp->SetStatusCode(404);
        resp->AddHeader("Content-Type", "text/plain; charset=utf-8");
        resp->AppendBody("404 Not Found: " + url_path);
        return;
    }

    std::ifstream ifs(file, std::ios::binary);
    if (!ifs) {
        resp->SetStatusCode(500);
        resp->AppendBody("500 cannot open file");
        return;
    }
    std::ostringstream oss;
    oss << ifs.rdbuf();
    std::string body = oss.str();

    resp->SetStatusCode(200);
    resp->AddHeader("Content-Type",   GuessContentType(file));
    resp->AddHeader("Content-Length", std::to_string(body.size()));
    resp->AddHeader("Alt-Svc",
                    "h3=\":" + std::to_string(h3_port) + "\"; ma=86400");
    resp->AppendBody(body);

    std::cout << "[h3 200] " << url_path << " -> " << file.string()
              << " (" << body.size() << " B)" << std::endl;
}

// ---- arg parsing -----------------------------------------------------------

struct Options {
    std::string doc_root  = "./www";
    std::string cert_path = "./cert.pem";
    std::string key_path  = "./key.pem";
    std::string host      = "0.0.0.0";
    uint16_t h3_port      = 7010;
    uint16_t http_port    = 8080;   // 0 -> disable plaintext upgrade port
    uint16_t https_port   = 8443;   // 0 -> disable TLS upgrade port
    bool     no_upgrade   = false;
};

static void PrintUsageAndExit(const char* argv0, int code) {
    std::cerr <<
        "Usage: " << argv0 << " [options]\n"
        "  --doc-root <dir>     (default ./www)\n"
        "  --cert <file>        (default ./cert.pem)\n"
        "  --key  <file>        (default ./key.pem)\n"
        "  --host <addr>        (default 0.0.0.0)\n"
        "  --h3-port <port>     (default 7010, UDP)\n"
        "  --http-port <port>   (default 8080,  TCP, 0=disable)\n"
        "  --https-port <port>  (default 8443,  TCP, 0=disable)\n"
        "  --no-upgrade         do not start the TCP upgrade endpoint\n";
    std::exit(code);
}

static Options ParseArgs(int argc, char* argv[]) {
    Options o;
    auto need = [&](int i){
        if (i + 1 >= argc) {
            std::cerr << "Missing value for " << argv[i] << "\n";
            PrintUsageAndExit(argv[0], 2);
        }
    };
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if      (a == "--doc-root")    { need(i); o.doc_root  = argv[++i]; }
        else if (a == "--cert")        { need(i); o.cert_path = argv[++i]; }
        else if (a == "--key")         { need(i); o.key_path  = argv[++i]; }
        else if (a == "--host")        { need(i); o.host      = argv[++i]; }
        else if (a == "--h3-port")     { need(i); o.h3_port   = static_cast<uint16_t>(std::stoi(argv[++i])); }
        else if (a == "--http-port")   { need(i); o.http_port = static_cast<uint16_t>(std::stoi(argv[++i])); }
        else if (a == "--https-port")  { need(i); o.https_port= static_cast<uint16_t>(std::stoi(argv[++i])); }
        else if (a == "--no-upgrade")  { o.no_upgrade = true; }
        else if (a == "-h" || a == "--help") { PrintUsageAndExit(argv[0], 0); }
        else {
            std::cerr << "Unknown option: " << a << "\n";
            PrintUsageAndExit(argv[0], 2);
        }
    }
    return o;
}

// ---- upgrade thread --------------------------------------------------------
//
// The upgrade module is event-loop driven. We run it on a dedicated thread so
// it doesn't compete with the H3 server's own worker threads.
//
// IMPORTANT thread-affinity rule:
//   `EventLoop::Init()` records `std::this_thread::get_id()` and from that
//   point on every `RegisterFd / AddTimer / RemoveFd / ...` call goes through
//   `AssertInLoopThread()` -- a hard `std::abort()` on mismatch.
//   That means BOTH `Init()` AND `AddListener()` (which calls RegisterFd
//   internally) must run on the same thread that will later call `Wait()`.
//
//   Earlier we did Init+AddListener on the main thread and then ran Wait()
//   on a worker -- the very first incoming connection's `accept()` callback
//   then tried to RegisterFd() the new client_fd from the worker thread,
//   tripping AssertInLoopThread() and (silently, in release-style logger
//   builds without an audible LOG_FATAL flush) leaving the new fd
//   un-monitored. Curl saw `Connected` (TCP works in the kernel) but never
//   got a ServerHello back -- a perfect "TLS hangs forever" symptom.
struct UpgradeRuntime {
    std::shared_ptr<quicx::common::IEventLoop> loop;
    std::unique_ptr<quicx::upgrade::IUpgrade>  server;
    std::thread                                thr;
    std::atomic<bool>                          running{false};
    std::atomic<bool>                          init_ok{false};
    std::atomic<bool>                          init_done{false};
};

static bool StartUpgrade(UpgradeRuntime& rt, const Options& opt) {
    rt.running.store(true, std::memory_order_release);

    rt.thr = std::thread([&rt, opt]() {
        // 1. Init / AddListener on THIS thread so EventLoop's recorded
        //    thread_id_ matches the thread that drives Wait().
        rt.loop = quicx::common::MakeEventLoop();
        if (!rt.loop || !rt.loop->Init()) {
            std::cerr << "[upgrade] failed to init event loop\n";
            rt.init_done.store(true, std::memory_order_release);
            return;
        }
        rt.server = quicx::upgrade::IUpgrade::MakeUpgrade(rt.loop);
        if (!rt.server) {
            std::cerr << "[upgrade] MakeUpgrade returned null\n";
            rt.init_done.store(true, std::memory_order_release);
            return;
        }

        quicx::upgrade::UpgradeSettings s;
        s.listen_addr  = opt.host;
        s.http_port    = opt.http_port;
        s.https_port   = opt.https_port;
        s.h3_port      = opt.h3_port;       // for Alt-Svc advertisement only
        s.enable_http1 = (opt.http_port  != 0);
        s.enable_http2 = (opt.https_port != 0);
        s.enable_http3 = true;              // advertise h3
        if (opt.https_port != 0) {
            s.cert_file = opt.cert_path;
            s.key_file  = opt.key_path;
        }
        s.log_level = quicx::LogLevel::kInfo;

        if (!rt.server->AddListener(s)) {
            std::cerr << "[upgrade] AddListener failed (host=" << opt.host
                      << " http=" << opt.http_port
                      << " https=" << opt.https_port << ")\n";
            rt.init_done.store(true, std::memory_order_release);
            return;
        }

        rt.init_ok.store(true, std::memory_order_release);
        rt.init_done.store(true, std::memory_order_release);

        // 2. Pump the event loop forever (Wait() returns on each iteration
        //    after firing fd/timer/task callbacks).
        while (rt.running.load(std::memory_order_acquire)) {
            rt.loop->Wait();
        }
    });

    // Wait for the worker thread to finish initialising so the caller can
    // report success/failure synchronously and so subsequent shutdown logic
    // sees a fully-constructed `rt.server`.
    while (!rt.init_done.load(std::memory_order_acquire)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    if (!rt.init_ok.load(std::memory_order_acquire)) {
        rt.running.store(false, std::memory_order_release);
        if (rt.thr.joinable()) rt.thr.join();
        return false;
    }
    return true;
}

static void StopUpgrade(UpgradeRuntime& rt) {
    rt.running.store(false, std::memory_order_release);
    // Kick the loop so Wait() returns promptly even when idle.
    if (rt.loop) {
        rt.loop->PostTask([]() {});
    }
    if (rt.thr.joinable()) rt.thr.join();
    rt.server.reset();
    rt.loop.reset();
}

// ---- main ------------------------------------------------------------------

int main(int argc, char* argv[]) {
    Options opt = ParseArgs(argc, argv);

    // Install a stdout logger BEFORE anything in quicX runs, so the upgrade
    // module's diagnostics (especially "[TLSDBG] ..." during TLS handshake,
    // "Failed to bind socket. errno: 48" on EADDRINUSE, "Failed to load
    // certificate file: ..." on bad PEM, etc.) are visible. BaseLogger uses
    // a write-once CAS sink, so whoever installs first wins -- doing it
    // here means every component in the process logs to stdout.
    LOG_SET(std::make_shared<quicx::common::StdoutLogger>());
    LOG_SET_LEVEL(quicx::common::LogLevel::kInfo);

    if (!fs::exists(opt.doc_root)) {
        fs::create_directories(opt.doc_root);
        std::ofstream(opt.doc_root + "/index.html")
            << "<!doctype html><meta charset=utf-8>"
               "<title>quicX H3 static</title>"
               "<h1>It works over HTTP/3!</h1>";
        std::cout << "Created sample doc root: " << opt.doc_root << std::endl;
    }

    auto slurp = [](const std::string& p) {
        std::ifstream f(p);
        std::ostringstream o; o << f.rdbuf(); return o.str();
    };
    std::string cert_pem = slurp(opt.cert_path);
    std::string key_pem  = slurp(opt.key_path);
    if (cert_pem.empty() || key_pem.empty()) {
        std::cerr << "ERROR: failed to read cert/key. "
                  << "Generate them via run.sh or openssl.\n";
        return 1;
    }

    std::signal(SIGINT,  HandleSignal);
    std::signal(SIGTERM, HandleSignal);

    // ------------------------------------------------------------------
    // 1. HTTP/3 server (real file-serving)
    // ------------------------------------------------------------------
    auto h3 = quicx::IServer::Create();

    quicx::Http3ServerConfig cfg;
    // NOTE: Http3ServerConfig stores raw `const char*` pointers, so the
    // backing std::string objects (cert_pem / key_pem) MUST stay alive for
    // the entire lifetime of the server. They live in main() until return,
    // which is fine here.
    cfg.quic_config_.cert_pem_ = cert_pem.c_str();
    cfg.quic_config_.key_pem_  = key_pem.c_str();
    cfg.quic_config_.config_.thread_mode_       = quicx::ThreadMode::kMultiThread;
    cfg.quic_config_.config_.worker_thread_num_ = 2;
    cfg.quic_config_.config_.log_level_         = quicx::LogLevel::kInfo;
    cfg.quic_config_.config_.log_path_          = "/tmp/h3_static_logs";
    h3->Init(cfg);

    fs::path root_abs = fs::absolute(opt.doc_root);
    uint16_t h3_port  = opt.h3_port;
    h3->AddHandler(quicx::HttpMethod::kGet, "/*",
        [root_abs, h3_port](std::shared_ptr<quicx::IRequest>  req,
                            std::shared_ptr<quicx::IResponse> resp) {
            ServeFile(root_abs, h3_port, req, resp);
        });

    if (!h3->Start(opt.host, opt.h3_port)) {
        std::cerr << "Failed to start HTTP/3 server on UDP "
                  << opt.host << ":" << opt.h3_port << std::endl;
        return 1;
    }
    std::cout << "[h3] listening on UDP "
              << opt.host << ":" << opt.h3_port
              << ", doc_root=" << root_abs.string() << std::endl;

    // ------------------------------------------------------------------
    // 2. Upgrade endpoint (TCP) -- optional but on by default
    // ------------------------------------------------------------------
    UpgradeRuntime up;
    bool upgrade_ok = false;
    if (!opt.no_upgrade && (opt.http_port || opt.https_port)) {
        upgrade_ok = StartUpgrade(up, opt);
        if (upgrade_ok) {
            std::cout << "[upgrade] running on " << opt.host
                      << " (http=" << opt.http_port
                      << ", https=" << opt.https_port
                      << "), advertising h3 on :" << opt.h3_port << std::endl;
        } else {
            std::cerr << "[upgrade] disabled due to startup failure; "
                         "H3 server still running" << std::endl;
        }
    }

    std::cout <<
        "Open in Chrome:\n"
        "  google-chrome --user-data-dir=/tmp/chrome-h3 "
        "--ignore-certificate-errors "
        "--origin-to-force-quic-on=" << opt.host << ":" << opt.h3_port << " "
        "https://" << opt.host << ":"
        << (opt.https_port ? opt.https_port : opt.h3_port) << "/\n";

    // ------------------------------------------------------------------
    // 3. Wait for shutdown
    // ------------------------------------------------------------------
    while (!g_shutdown.load(std::memory_order_acquire)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    std::cout << "shutting down..." << std::endl;

    if (upgrade_ok) StopUpgrade(up);
    h3->Stop();
    h3->Join();
    return 0;
}
