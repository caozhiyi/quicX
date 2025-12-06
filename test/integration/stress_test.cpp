// Stress Test
// Long-running and high-concurrency tests

#include <gtest/gtest.h>
#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

#include "http3/include/if_client.h"
#include "http3/include/if_request.h"
#include "http3/include/if_response.h"
#include "http3/include/if_server.h"

class StressTest: public ::testing::Test {
protected:
    std::shared_ptr<quicx::IServer> server_;
    std::thread server_thread_;
    uint16_t port_ = 18446;

    static const char cert_pem_[];
    static const char key_pem_[];

    void SetUp() override {
        server_ = quicx::IServer::Create();

        quicx::Http3ServerConfig server_config;
        server_config.cert_pem_ = cert_pem_;
        server_config.key_pem_ = key_pem_;
        server_config.config_.thread_num_ = 4;
        server_config.config_.log_level_ = quicx::LogLevel::kError;

        ASSERT_TRUE(server_->Init(server_config));

        server_->AddHandler(quicx::HttpMethod::kGet, "/test",
            [](std::shared_ptr<quicx::IRequest> req, std::shared_ptr<quicx::IResponse> resp) {
                resp->SetStatusCode(200);
                resp->AppendBody("OK");
            });

        server_->AddHandler(quicx::HttpMethod::kGet, "/data",
            [](std::shared_ptr<quicx::IRequest> req, std::shared_ptr<quicx::IResponse> resp) {
                std::string data(1024, 'X');  // 1KB
                resp->SetStatusCode(200);
                resp->AppendBody(data);
            });

        server_thread_ = std::thread([this]() { server_->Start("127.0.0.1", port_); });

        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    void TearDown() override {
        server_->Stop();
        if (server_thread_.joinable()) {
            server_thread_.join();
        }
    }
};

const char StressTest::cert_pem_[] =
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

const char StressTest::key_pem_[] =
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

TEST_F(StressTest, HighConcurrency) {
    const int num_clients = 50;
    const int requests_per_client = 10;

    std::atomic<int> success_count{0};
    std::atomic<int> total_count{0};
    std::vector<std::thread> threads;

    auto start = std::chrono::steady_clock::now();

    for (int i = 0; i < num_clients; ++i) {
        threads.emplace_back([&, i]() {
            auto client = quicx::IClient::Create();

            quicx::Http3Config config;
            config.thread_num_ = 1;
            config.log_level_ = quicx::LogLevel::kError;

            if (!client->Init(config)) {
                return;
            }

            std::string url = "https://127.0.0.1:" + std::to_string(port_) + "/test";

            for (int j = 0; j < requests_per_client; ++j) {
                auto request = quicx::IRequest::Create();
                std::atomic<bool> completed{false};

                total_count++;

                client->DoRequest(url, quicx::HttpMethod::kGet, request,
                    [&](std::shared_ptr<quicx::IResponse> response, uint32_t error) {
                        if (error == 0) {
                            success_count++;
                        }
                        completed = true;
                    });

                for (int k = 0; k < 100 && !completed; ++k) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                }
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    auto end = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::seconds>(end - start);

    int total = total_count.load();
    int success = success_count.load();

    std::cout << "High Concurrency Test Results:" << std::endl;
    std::cout << "  Clients: " << num_clients << std::endl;
    std::cout << "  Total requests: " << total << std::endl;
    std::cout << "  Successful: " << success << std::endl;
    std::cout << "  Duration: " << duration.count() << "s" << std::endl;
    std::cout << "  Throughput: " << (success / duration.count()) << " req/s" << std::endl;

    // At least 80% success rate
    EXPECT_GT(success, total * 0.8);
}

TEST_F(StressTest, SustainedLoad) {
    const int duration_seconds = 10;  // 10 second test
    const int num_clients = 10;

    std::atomic<int> success_count{0};
    std::atomic<bool> stop{false};
    std::vector<std::thread> threads;

    auto start = std::chrono::steady_clock::now();

    for (int i = 0; i < num_clients; ++i) {
        threads.emplace_back([&]() {
            auto client = quicx::IClient::Create();

            quicx::Http3Config config;
            config.thread_num_ = 1;
            config.log_level_ = quicx::LogLevel::kError;

            if (!client->Init(config)) {
                return;
            }

            std::string url = "https://127.0.0.1:" + std::to_string(port_) + "/test";

            while (!stop) {
                auto request = quicx::IRequest::Create();
                std::atomic<bool> completed{false};

                client->DoRequest(url, quicx::HttpMethod::kGet, request,
                    [&](std::shared_ptr<quicx::IResponse> response, uint32_t error) {
                        if (error == 0) {
                            success_count++;
                        }
                        completed = true;
                    });

                for (int k = 0; k < 50 && !completed && !stop; ++k) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                }
            }
        });
    }

    // Run for specified duration
    std::this_thread::sleep_for(std::chrono::seconds(duration_seconds));
    stop = true;

    for (auto& t : threads) {
        t.join();
    }

    auto end = std::chrono::steady_clock::now();
    auto actual_duration = std::chrono::duration_cast<std::chrono::seconds>(end - start);

    int success = success_count.load();

    std::cout << "Sustained Load Test Results:" << std::endl;
    std::cout << "  Duration: " << actual_duration.count() << "s" << std::endl;
    std::cout << "  Successful requests: " << success << std::endl;
    std::cout << "  Average throughput: " << (success / actual_duration.count()) << " req/s" << std::endl;

    // Should handle sustained load
    EXPECT_GT(success, 100);  // At least 100 requests in 10 seconds
}

TEST_F(StressTest, LargeDataTransfer) {
    auto client = quicx::IClient::Create();

    quicx::Http3Config config;
    config.thread_num_ = 2;
    config.log_level_ = quicx::LogLevel::kError;

    ASSERT_TRUE(client->Init(config));

    const int num_requests = 100;
    std::atomic<int> success_count{0};
    std::atomic<size_t> total_bytes{0};

    std::string url = "https://127.0.0.1:" + std::to_string(port_) + "/data";

    auto start = std::chrono::steady_clock::now();

    for (int i = 0; i < num_requests; ++i) {
        auto request = quicx::IRequest::Create();
        std::atomic<bool> completed{false};

        client->DoRequest(
            url, quicx::HttpMethod::kGet, request, [&](std::shared_ptr<quicx::IResponse> response, uint32_t error) {
                if (error == 0) {
                    success_count++;
                    total_bytes += response->GetBodyAsString().size();
                }
                completed = true;
            });

        for (int j = 0; j < 100 && !completed; ++j) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }

    auto end = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    double throughput_mbps = (total_bytes / 1024.0 / 1024.0) / (duration.count() / 1000.0);

    std::cout << "Large Data Transfer Test Results:" << std::endl;
    std::cout << "  Requests: " << num_requests << std::endl;
    std::cout << "  Successful: " << success_count.load() << std::endl;
    std::cout << "  Total bytes: " << total_bytes.load() << std::endl;
    std::cout << "  Throughput: " << throughput_mbps << " MB/s" << std::endl;

    EXPECT_EQ(success_count.load(), num_requests);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
