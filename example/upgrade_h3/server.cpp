// upgrade_h3 example: minimal driver to exercise the quicX `upgrade` module
// in isolation (no QUIC/H3 server next to it).
//
// What this binary does:
//   * Hands the port configuration to IUpgrade and lets the module run.
//     The upgrade server owns a private EventLoop and the thread that
//     drives it, so this file no longer has to care about EventLoop's
//     thread-affinity contract (Init / RegisterFd / AddTimer / Wait must
//     all happen on one thread -- violating it aborts the process, which
//     is exactly what an earlier version of this example did by running
//     Init() on main and Wait() on a worker: curl reported "Connected"
//     and then hung forever waiting for a ServerHello).
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
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <quicx/upgrade/if_upgrade.h>
#include <string>
#include <thread>

#include "common/log/log.h"
#include "common/log/stdout_logger.h"

using quicx::IUpgrade;
using quicx::UpgradeSettings;

namespace {

std::atomic<bool> g_stop{false};
void HandleSignal(int) {
    g_stop.store(true, std::memory_order_release);
}

struct CliOptions {
    std::string host = "0.0.0.0";
    uint16_t http_port = 8080;
    uint16_t https_port = 8443;
    uint16_t h3_port = 8443;
    std::string cert_file;  // optional; required to enable HTTPS listener
    std::string key_file;
};

void PrintUsage(const char* argv0) {
    std::cerr << "Usage: " << argv0
              << " [options]\n"
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
        if (a == "--host") {
            if (!need(i)) return false;
            out.host = argv[++i];
        } else if (a == "--http-port") {
            if (!need(i)) return false;
            out.http_port = static_cast<uint16_t>(std::stoi(argv[++i]));
        } else if (a == "--https-port") {
            if (!need(i)) return false;
            out.https_port = static_cast<uint16_t>(std::stoi(argv[++i]));
        } else if (a == "--h3-port") {
            if (!need(i)) return false;
            out.h3_port = static_cast<uint16_t>(std::stoi(argv[++i]));
        } else if (a == "--cert") {
            if (!need(i)) return false;
            out.cert_file = argv[++i];
        } else if (a == "--key") {
            if (!need(i)) return false;
            out.key_file = argv[++i];
        } else if (a == "-h" || a == "--help") {
            PrintUsage(argv[0]);
            std::exit(0);
        } else {
            std::cerr << "Unknown option: " << a << "\n";
            PrintUsage(argv[0]);
            return false;
        }
    }
    return true;
}

}  // namespace

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
    settings.listen_addr_ = opt.host;
    settings.http_port_ = opt.http_port;
    settings.https_port_ = opt.https_port;
    settings.h3_port_ = opt.h3_port;
    settings.enable_http1_ = (opt.http_port != 0);
    settings.enable_http2_ = (opt.https_port != 0);
    settings.enable_http3_ = true;  // Alt-Svc advertises h3 only
    if (!opt.cert_file.empty()) settings.cert_file_ = opt.cert_file;
    if (!opt.key_file.empty()) settings.key_file_ = opt.key_file;

    // -----------------------------------------------------------------------
    // Step 3: start the upgrade server.
    //
    // The server creates its own EventLoop and the thread that drives it on
    // the first AddListener(); AddListener() itself blocks until the sockets
    // are bound and registered, so a successful return means it is serving.
    auto server = IUpgrade::MakeUpgrade();
    if (!server) {
        std::cerr << "Failed to create upgrade server" << std::endl;
        return EXIT_FAILURE;
    }

    if (!server->AddListener(settings)) {
        // The real reason (EADDRINUSE / cert load failure / ...) was just
        // emitted via LOG_ERROR by the upgrade module itself; check stdout.
        std::cerr << "Failed to add listener (see LOG_ERROR above for the real cause)" << std::endl;
        return EXIT_FAILURE;
    }

    std::signal(SIGINT, HandleSignal);
    std::signal(SIGTERM, HandleSignal);

    std::cout << "upgrade_h3_server running on " << settings.listen_addr_ << " (http=" << settings.http_port_
              << ", https=" << settings.https_port_ << "), advertising h3 on :" << settings.h3_port_ << std::endl
              << "Press Ctrl+C to stop." << std::endl;

    // -----------------------------------------------------------------------
    // Step 4: idle until SIGINT. The upgrade server runs on its own thread,
    // so main() only has to stay alive.
    while (!g_stop.load(std::memory_order_acquire)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    std::cout << "shutting down..." << std::endl;
    // Stops the loop thread and closes every listener / client socket.
    server->Stop();
    return EXIT_SUCCESS;
}
