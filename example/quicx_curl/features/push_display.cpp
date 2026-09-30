#include "features/push_display.h"

#include <cstdio>

#include <quicx/http3/if_client.h>
#include <quicx/http3/if_response.h>

namespace {

std::string HeaderOr(const quicx::HttpFields& h, const std::string& key) {
    for (const auto& kv : h)
        if (kv.first == key) return kv.second;
    return "";
}

}  // namespace

void InstallPushHandlers(quicx::IClient& client, const Tracer& tracer) {
    client.SetPushPromiseHandler([&tracer](quicx::HttpFields& headers) -> bool {
        std::string path = HeaderOr(headers, ":path");
        std::string authority = HeaderOr(headers, ":authority");
        std::fprintf(stderr, "* push promise: https://%s%s — accepted\n",
            authority.c_str(), path.c_str());
        (void)tracer;
        return true;
    });

    client.SetPushHandler([&tracer](std::shared_ptr<quicx::IResponse> resp, uint32_t error) {
        if (error != 0 || !resp) {
            std::fprintf(stderr, "* push stream error: %u\n", error);
            return;
        }
        std::string body = resp->GetBodyAsString();
        std::string type = HeaderOr(resp->GetHeaders(), "content-type");
        std::fprintf(stderr, "* push response: HTTP/3 %u, %llu bytes%s%s\n",
            resp->GetStatusCode(), static_cast<unsigned long long>(body.size()),
            type.empty() ? "" : " (", type.c_str());
        if (!type.empty()) std::fprintf(stderr, ")");
        std::fprintf(stderr, "\n");
        (void)tracer;
    });
}
