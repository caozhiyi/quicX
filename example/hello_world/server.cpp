#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <quicx/http3/if_request.h>
#include <quicx/http3/if_response.h>
#include <quicx/http3/if_server.h>

// Use an atomic flag instead of calling Stop() directly from the signal
// handler: Stop() -> Destroy() takes locks and frees memory, neither of which
// is async-signal-safe. Calling them from the signal context aborts (verified
// experimentally — server SIGABRTs and heaptrack output is 0 bytes).
static std::atomic<bool> g_shutdown{false};

static void HandleSignal(int) {
    g_shutdown.store(true, std::memory_order_release);
}

int main(int argc, char** argv) {
    // Allow overriding the listen port so tooling (e.g. run_tests.py running
    // multiple example tests concurrently) can avoid binding the same UDP
    // port from two different processes. Resolution order:
    //   1. argv[1]                    -> ./hello_world_server 7011
    //   2. env QUICX_HELLO_WORLD_PORT -> QUICX_HELLO_WORLD_PORT=7011 ./hello_world_server
    //   3. default 7001
    uint16_t listen_port = 7001;
    auto parse_port = [](const std::string& s, uint16_t& out) -> bool {
        try {
            unsigned long v = std::stoul(s);
            if (v == 0 || v > 65535) return false;
            out = static_cast<uint16_t>(v);
            return true;
        } catch (...) {
            return false;
        }
    };
    if (argc > 1) {
        if (!parse_port(argv[1], listen_port)) {
            std::cerr << "invalid port argument: " << argv[1] << std::endl;
            return 2;
        }
    } else if (const char* env_port = std::getenv("QUICX_HELLO_WORLD_PORT")) {
        if (env_port[0] != '\0' && !parse_port(env_port, listen_port)) {
            std::cerr << "invalid QUICX_HELLO_WORLD_PORT value: " << env_port << std::endl;
            return 2;
        }
    }

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

    std::signal(SIGINT, HandleSignal);
    std::signal(SIGTERM, HandleSignal);

    auto server = quicx::IServer::Create();
    server->AddHandler(quicx::HttpMethod::kGet, "/hello",
        [](std::shared_ptr<quicx::IRequest> req, std::shared_ptr<quicx::IResponse> resp) {
            resp->AppendBody(std::string("hello world"));
            resp->SetStatusCode(200);
        });
    quicx::Http3ServerConfig config;
    config.quic_config_.cert_pem_ = cert_pem;
    config.quic_config_.key_pem_ = key_pem;
    config.quic_config_.config_.thread_mode_ = quicx::ThreadMode::kMultiThread;
    config.quic_config_.config_.worker_thread_num_ = 4;
    config.quic_config_.config_.log_level_ = quicx::LogLevel::kError;
    config.quic_config_.config_.log_path_ = "/tmp/h3_server_logs";

    // Enable QLog so we can visualize the connection in qvis
    config.quic_config_.config_.qlog_config_.enabled = false;
    config.quic_config_.config_.qlog_config_.output_dir = "./qlog_output_server";
    config.quic_config_.config_.qlog_config_.flush_interval_ms = 100;

    // Expose Prometheus metrics over HTTP/3 at GET /metrics so we can probe
    // server internal state with: quicx-curl https://localhost:7001/metrics
    config.metrics_.enable = true;
    config.metrics_.http_enable = true;
    config.metrics_.http_path = "/metrics";

    server->Init(config);
    if (!server->Start("0.0.0.0", listen_port)) {
        std::cout << "start server failed (port " << listen_port << ")" << std::endl;
        return 1;
    }
    std::cout << "hello_world_server listening on 0.0.0.0:" << listen_port << std::endl;

    // Watchdog: when SIGINT/SIGTERM sets g_shutdown, call Stop() from a normal
    // thread context (NOT signal context) so Destroy()/locks/free are safe.
    std::thread shutdown_thread([&server]() {
        while (!g_shutdown.load(std::memory_order_acquire)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        std::cout << "shutdown signal received, stopping server" << std::endl;
        server->Stop();
    });

    server->Join();
    shutdown_thread.join();
    return 0;
}
