#include "features/redirect.h"

#include "app/url.h"

namespace {

bool IsRedirectStatus(uint32_t status) {
    return status == 301 || status == 302 || status == 303 || status == 307 || status == 308;
}

// curl semantics: 301/302/303 rewrite POST (and friends) to GET and drop the
// body unless the method was pinned with -X. 307/308 always preserve.
bool RewritesToGet(uint32_t status, quicx::HttpMethod method) {
    if (status == 307 || status == 308) return false;
    switch (method) {
        case quicx::HttpMethod::kPost:
        case quicx::HttpMethod::kPut:
        case quicx::HttpMethod::kPatch:
            return true;
        default:
            return false;
    }
}

}  // namespace

RedirectDecision RedirectPolicy::Evaluate(const RequestResult& result, const std::string& current_url,
    quicx::HttpMethod method) {
    RedirectDecision d;
    if (!follow_) return d;
    if (!IsRedirectStatus(result.status)) return d;
    if (hops_used_ >= max_redirects_) return d;

    std::string location = result.Header("location");
    if (location.empty()) return d;  // 3xx without Location: not followable

    d.next_url = JoinUrl(current_url, location);
    d.switch_to_get = RewritesToGet(result.status, method);
    d.follow = true;
    ++hops_used_;
    return d;
}
