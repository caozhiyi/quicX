#include <thread>
#include <csignal>
#include <cstdlib>
#include <iostream>

#include "upgrade/include/if_upgrade.h"
#include "common/network/if_event_loop.h"

using quicx::upgrade::IUpgrade;
using quicx::upgrade::UpgradeSettings;

static volatile std::sig_atomic_t g_stop = 0;

static void handle_signal(int) {
    g_stop = 1;
}

int main(int argc, char** argv) {
    UpgradeSettings settings;
    settings.listen_addr = "0.0.0.0";
    settings.http_port = 8080;   // HTTP port（without certificate）
    settings.https_port = 8443;  // HTTPS port（with certificate）
    settings.h3_port = 8443;     // h3 port（usually the same as https）
    settings.enable_http1 = true;
    settings.enable_http2 = true;
    settings.enable_http3 = true; // allow negotiation/advertise h3

    auto event_loop = quicx::common::MakeEventLoop();
    if (!event_loop->Init()) {
        std::cerr << "Failed to init event loop" << std::endl;
        return EXIT_FAILURE;
    }
    auto server = IUpgrade::MakeUpgrade(event_loop);

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

    std::thread([&]() {
        while (!g_stop) {
            event_loop->Wait();
        }
    }).join();

    return EXIT_SUCCESS;
}


