#include "app/request_builder.h"

#include <cstdio>
#include <quicx/http3/if_request.h>
#include <quicx/http3/type.h>

#include "app/base64.h"
#include "app/url.h"
#include "features/cookie_jar.h"

namespace {

std::string ReadFileToString(const std::string& path, bool& ok) {
    ok = false;
#ifdef _WIN32
    std::FILE* f = nullptr;
    if (fopen_s(&f, path.c_str(), "rb") != 0 || !f) return "";
#else
    std::FILE* f = std::fopen(path.c_str(), "rb");
#endif
    if (!f) return "";
    std::string data;
    char buf[16384];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) data.append(buf, n);
    std::fclose(f);
    ok = true;
    return data;
}

void AddRawHeader(quicx::IRequest* req, const std::string& raw) {
    size_t colon = raw.find(':');
    if (colon == std::string::npos) return;  // "-H Name;" removal not supported
    std::string name = raw.substr(0, colon);
    std::string value = raw.substr(colon + 1);
    while (!value.empty() && (value[0] == ' ' || value[0] == '\t')) value.erase(0, 1);
    req->AddHeader(name, value);
}

}  // namespace

quicx::HttpMethod MethodFromString(const std::string& method) {
    if (method == "POST") return quicx::HttpMethod::kPost;
    if (method == "PUT") return quicx::HttpMethod::kPut;
    if (method == "DELETE") return quicx::HttpMethod::kDelete;
    if (method == "HEAD") return quicx::HttpMethod::kHead;
    if (method == "OPTIONS") return quicx::HttpMethod::kOptions;
    if (method == "PATCH") return quicx::HttpMethod::kPatch;
    if (method == "TRACE") return quicx::HttpMethod::kTrace;
    if (method == "CONNECT") return quicx::HttpMethod::kConnect;
    return quicx::HttpMethod::kGet;
}

std::string MethodToString(quicx::HttpMethod method) {
    switch (method) {
        case quicx::HttpMethod::kPost: return "POST";
        case quicx::HttpMethod::kPut: return "PUT";
        case quicx::HttpMethod::kDelete: return "DELETE";
        case quicx::HttpMethod::kHead: return "HEAD";
        case quicx::HttpMethod::kOptions: return "OPTIONS";
        case quicx::HttpMethod::kPatch: return "PATCH";
        case quicx::HttpMethod::kTrace: return "TRACE";
        case quicx::HttpMethod::kConnect: return "CONNECT";
        default: return "GET";
    }
}

std::shared_ptr<quicx::IRequest> BuildRequest(const RequestSpec& spec, const CookieJar* jar,
    const std::string& url) {
    auto req = quicx::IRequest::Create();
    if (!req) return nullptr;

    // -H raw headers first so later flags can still add theirs
    for (const auto& h : spec.headers) AddRawHeader(req.get(), h);

    if (!spec.user_agent.empty()) req->AddHeader("user-agent", spec.user_agent);
    else req->AddHeader("user-agent", "quicx-curl/1.0");

    if (!spec.referer.empty()) req->AddHeader("referer", spec.referer);

    if (!spec.user.empty()) {
        req->AddHeader("authorization", "Basic " + Base64Encode(spec.user));
    }

    if (jar) {
        std::string cookies = jar->CookieHeaderFor(url);
        if (!cookies.empty()) req->AddHeader("cookie", cookies);
    }

    if (!spec.upload_file.empty()) {
        // -T: stream from file via provider; ownership stays with the lambda.
        std::FILE* f = nullptr;
#ifdef _WIN32
        if (fopen_s(&f, spec.upload_file.c_str(), "rb") != 0) f = nullptr;
#else
        f = std::fopen(spec.upload_file.c_str(), "rb");
#endif
        if (!f) return nullptr;
        auto holder = std::make_shared<std::FILE*>(f);
        req->SetRequestBodyProvider([holder](uint8_t* buf, size_t size) -> size_t {
            if (!*holder) return 0;
            size_t n = std::fread(buf, 1, size, *holder);
            if (n == 0) {
                std::fclose(*holder);
                *holder = nullptr;
            }
            return n;
        });
    } else if (!spec.data_file.empty()) {
        bool ok = false;
        std::string data = ReadFileToString(spec.data_file, ok);
        if (!ok) return nullptr;
        req->AppendBody(data);
    } else if (!spec.data.empty()) {
        req->AppendBody(spec.data);
    }

    // curl default content-type for -d (when user did not set one)
    if ((spec.data.size() || !spec.data_file.empty()) && !spec.upload_file.size()) {
        std::string tmp;
        if (!req->GetHeader("content-type", tmp)) req->AddHeader("content-type", "application/x-www-form-urlencoded");
    }

    return req;
}
