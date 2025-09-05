#if defined(QUICX_ENABLE_BENCHMARKS)
#include <benchmark/benchmark.h>
#include <memory>
#include <string>
#include <thread>

#include "http3/include/if_server.h"
#include "http3/include/if_client.h"
#include "http3/include/if_request.h"
#include "http3/include/if_response.h"

namespace quicx {
namespace http3 {

static const char kCert[] =
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

static const char kKey[] =
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

static void BM_H3_E2E_Request_Response(benchmark::State& state) {
    Http3Settings settings = kDefaultHttp3Settings;
    settings.enable_push = 1;
    auto server = IServer::Create(settings);
    server->AddHandler(HttpMethod::kGet, "/hello",
        [](std::shared_ptr<IRequest> /*req*/, std::shared_ptr<IResponse> resp) {
            resp->SetBody("hello world");
            resp->SetStatusCode(200);
            auto push_resp = IResponse::Create();
            push_resp->AddHeader("push-key1", "test1");
            push_resp->SetBody("hello push");
            push_resp->SetStatusCode(200);
            resp->AppendPush(push_resp);
        });
    Http3ServerConfig sc;
    sc.cert_pem_ = kCert;
    sc.key_pem_ = kKey;
    sc.config_.thread_num_ = 1;
    sc.config_.log_level_ = LogLevel::kError;
    server->Init(sc);

    std::thread th([&](){ server->Start("127.0.0.1", 8890); });
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    auto client = IClient::Create(settings);
    Http3Config cc;
    cc.thread_num_ = 1;
    client->Init(cc);
    client->SetPushPromiseHandler([](std::unordered_map<std::string, std::string>&){ return true; });
    client->SetPushHandler([](std::shared_ptr<IResponse> /*resp*/, uint32_t /*err*/){ });

    for (auto _ : state) {
        auto req = IRequest::Create();
        req->SetMethod(HttpMethod::kGet);
        req->SetPath("/hello");
        bool ok = client->DoRequest("https://127.0.0.1:8890/hello", HttpMethod::kGet, req,
            [](std::shared_ptr<IResponse> /*resp*/, uint32_t /*err*/){ });
        benchmark::DoNotOptimize(ok);
    }

    server->Stop();
    th.join();
}

} // namespace http3
} // namespace quicx

BENCHMARK(quicx::http3::BM_H3_E2E_Request_Response);
BENCHMARK_MAIN();
#else
int main() { return 0; }
#endif


