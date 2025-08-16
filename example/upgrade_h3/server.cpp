#include <csignal>
#include <cstdlib>
#include <iostream>
#include <thread>
#include <chrono>
#include "upgrade/include/if_upgrade.h"

using quicx::upgrade::IUpgrade;
using quicx::upgrade::UpgradeSettings;
using quicx::upgrade::LogLevel;

static volatile std::sig_atomic_t g_stop = 0;

static void handle_signal(int) {
    g_stop = 1;
}

int main(int argc, char** argv) {
    UpgradeSettings settings;
    settings.listen_addr = "0.0.0.0";
    settings.http_port = 8080;   // HTTP 端口（无证书时使用）
    settings.https_port = 8443;  // HTTPS 端口（提供证书时使用）
    settings.h3_port = 8443;     // h3 端口（通常与 https 相同）
    settings.enable_http1 = true;
    settings.enable_http2 = true;
    settings.enable_http3 = true; // 允许协商/广告 h3

    // 如果想启用 HTTPS，可设置证书（两种方式二选一）
    // settings.cert_file = "server.crt";
    // settings.key_file  = "server.key";
    // 或者：settings.cert_pem = <PEM内存指针>; settings.key_pem = <PEM内存指针>;

    auto server = IUpgrade::MakeUpgrade();
    if (!server->Init(LogLevel::kDebug)) {
        std::cerr << "Failed to init upgrade server" << std::endl;
        return EXIT_FAILURE;
    }

    if (!server->AddListener(settings)) {
        std::cerr << "Failed to add listener" << std::endl;
        return EXIT_FAILURE;
    }

    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);

    std::cout << "upgrade_h3_server running on "
              << settings.listen_addr << ":"
              << (settings.cert_file.empty() && settings.key_file.empty() ? settings.http_port : settings.https_port)
              << ", advertising h3 on :" << settings.h3_port << std::endl;

    while (!g_stop) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    server->Stop();
    server->Join();
    return EXIT_SUCCESS;
}


