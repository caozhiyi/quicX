// upgrade_h3 example: minimal driver to exercise the quicX `upgrade` module
// in isolation (no QUIC/H3 server next to it).
//
// What this binary does:
//   * Creates one event loop and runs Init() / AddListener() / Wait()
//     ALL on the main thread. This is REQUIRED -- EventLoop records
//     `thread_id_` in Init() and AssertInLoopThread()-aborts on any
//     RegisterFd / ModifyFd / AddTimer call from a different thread,
//     including the RegisterFd that ConnectionHandler::OnRead does for
//     each accepted client_fd. Splitting Init into the main thread and
//     Wait into a worker (the previous version of this file) caused the
//     classic "curl says Connected but never gets a byte back" symptom
//     because the very first accept tripped the assert (and silently
//     aborted, depending on logger configuration).
//
//   * Installs a StdoutLogger BEFORE creating the loop / upgrade server,
//     so AddListener's diagnostic LOG_ERROR lines (e.g. "Failed to bind
//     socket. errno: 48" for EADDRINUSE) actually reach the terminal.
//     Previously, with no logger installed, those messages were dropped
//     and you only saw the example's own "Failed to add listener" line
//     with no clue why.
//
// Usage:
//   upgrade_h3_server [--host 0.0.0.0] [--http-port 8080]
//                     [--https-port 8443] [--h3-port 8443]
//                     [--cert <pem>] [--key <pem>]
//
// Tests against this binary:
//   curl -v  http://127.0.0.1:8080/        # plain HTTP path (Alt-Svc stub)
//   curl -kv https://127.0.0.1:8443/       # TLS path (needs cert/key)

#include <atomic>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>

#include <quicx/upgrade/if_upgrade.h>
#include <quicx/common/if_event_loop.h>

#include "common/log/log.h"
#include "common/log/stdout_logger.h"

using quicx::upgrade::IUpgrade;
using quicx::upgrade::UpgradeSettings;

namespace {

std::atomic<bool> g_stop{false};
void HandleSignal(int) { g_stop.store(true, std::memory_order_release); }

struct CliOptions {
    std::string host       = "0.0.0.0";
    uint16_t    http_port  = 8080;
    uint16_t    https_port = 8443;
    uint16_t    h3_port    = 8443;
    std::string cert_file;   // optional; required to enable HTTPS listener
    std::string key_file;
};

void PrintUsage(const char* argv0) {
    std::cerr <<
        "Usage: " << argv0 << " [options]\n"
        "  --host <addr>        bind address (default 0.0.0.0)\n"
        "  --http-port <port>   plaintext HTTP port (default 8080, 0=disable)\n"
        "  --https-port <port>  TLS port (default 8443, 0=disable)\n"
        "  --h3-port <port>     port advertised in Alt-Svc (default 8443)\n"
        "  --cert <file>        PEM cert (required to enable HTTPS)\n"
        "  --key  <file>        PEM key  (required to enable HTTPS)\n";
}

bool ParseArgs(int argc, char** argv, CliOptions& out) {
    auto need = [&](int i) -> bool {
        if (i + 1 >= argc) {
            std::cerr << "Missing value for " << argv[i] << "\n";
            PrintUsage(argv[0]);
            return false;
        }
        return true;
    };
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if      (a == "--host")        { if (!need(i)) return false; out.host       = argv[++i]; }
        else if (a == "--http-port")   { if (!need(i)) return false; out.http_port  = static_cast<uint16_t>(std::stoi(argv[++i])); }
        else if (a == "--https-port")  { if (!need(i)) return false; out.https_port = static_cast<uint16_t>(std::stoi(argv[++i])); }
        else if (a == "--h3-port")     { if (!need(i)) return false; out.h3_port    = static_cast<uint16_t>(std::stoi(argv[++i])); }
        else if (a == "--cert")        { if (!need(i)) return false; out.cert_file  = argv[++i]; }
        else if (a == "--key")         { if (!need(i)) return false; out.key_file   = argv[++i]; }
        else if (a == "-h" || a == "--help") { PrintUsage(argv[0]); std::exit(0); }
        else {
            std::cerr << "Unknown option: " << a << "\n";
            PrintUsage(argv[0]);
            return false;
        }
    }
    return true;
}

} // namespace

int main(int argc, char** argv) {
    CliOptions opt;
    if (!ParseArgs(argc, argv, opt)) {
        return EXIT_FAILURE;
    }

    // -----------------------------------------------------------------------
    // Step 1: install a logger BEFORE anything else.
    //
    // BaseLogger::SetLogger is write-once (CAS on a null logger_); whoever
    // installs first wins. We do it here so every LOG_* from the upgrade
    // module / common networking layer is routed to stdout while we are
    // debugging this example. Without this call, LOG_ERROR messages are
    // silently dropped and AddListener failures look mysterious.
    LOG_SET(std::make_shared<quicx::common::StdoutLogger>());
    LOG_SET_LEVEL(quicx::common::LogLevel::kInfo);

    // -----------------------------------------------------------------------
    // Step 2: build the upgrade settings.
    UpgradeSettings settings;
    settings.listen_addr  = opt.host;
    settings.http_port    = opt.http_port;
    settings.https_port   = opt.https_port;
    settings.h3_port      = opt.h3_port;
    settings.enable_http1 = (opt.http_port  != 0);
    settings.enable_http2 = (opt.https_port != 0);
    settings.enable_http3 = true;       // Alt-Svc advertises h3 only
    if (!opt.cert_file.empty()) settings.cert_file = opt.cert_file;
    if (!opt.key_file.empty())  settings.key_file  = opt.key_file;

    // -----------------------------------------------------------------------
    // Step 3: create the event loop on THIS thread.
    //
    // EventLoop::Init() records std::this_thread::get_id() into thread_id_,
    // and every RegisterFd / ModifyFd / RemoveFd / AddTimer asserts that
    // the calling thread matches. ConnectionHandler::OnRead ALSO calls
    // RegisterFd (for each freshly accept()-ed client fd), so Wait() MUST
    // run on the same thread that called Init(). We do everything on
    // main() to satisfy that.
    auto event_loop = quicx::common::MakeEventLoop();
    if (!event_loop || !event_loop->Init()) {
        std::cerr << "Failed to init event loop" << std::endl;
        return EXIT_FAILURE;
    }

    auto server = IUpgrade::MakeUpgrade(event_loop);
    if (!server) {
        std::cerr << "Failed to create upgrade server" << std::endl;
        return EXIT_FAILURE;
    }

    if (!server->AddListener(settings)) {
        // The real reason (EADDRINUSE / cert load failure / ...) was just
        // emitted via LOG_ERROR by the upgrade module itself; check stdout.
        std::cerr << "Failed to add listener (see LOG_ERROR above for the real cause)"
                  << std::endl;
        return EXIT_FAILURE;
    }

    std::signal(SIGINT,  HandleSignal);
    std::signal(SIGTERM, HandleSignal);

    std::cout << "upgrade_h3_server running on " << settings.listen_addr
              << " (http=" << settings.http_port
              << ", https=" << settings.https_port
              << "), advertising h3 on :" << settings.h3_port
              << std::endl
              << "Press Ctrl+C to stop." << std::endl;

    // -----------------------------------------------------------------------
    // Step 4: drive the loop on the main thread until SIGINT.
    //
    // EventLoop::Wait() blocks at most until the next timer; we re-check
    // g_stop on each iteration so Ctrl+C terminates within ~1s even when
    // nothing else is happening.
    while (!g_stop.load(std::memory_order_acquire)) {
        event_loop->Wait();
    }

    std::cout << "shutting down..." << std::endl;
    return EXIT_SUCCESS;
}
