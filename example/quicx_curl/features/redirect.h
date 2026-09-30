#ifndef TOOL_QC_FEATURES_REDIRECT_H
#define TOOL_QC_FEATURES_REDIRECT_H

#include <string>

#include <quicx/http3/type.h>

#include "app/request_driver.h"

// Redirect policy for -L. Owns the 3xx decision table and curl's method
// rewrite rules; the orchestrator drives the hop loop.
struct RedirectDecision {
    bool follow = false;
    std::string next_url;               // resolved absolute URL
    bool switch_to_get = false;         // 301/302/303: POST/PUT -> GET, drop body
};

class RedirectPolicy {
public:
    RedirectPolicy(bool follow, int max_redirects) : follow_(follow), max_redirects_(max_redirects) {}

    int hops_used() const { return hops_used_; }

    // `result` is the finished hop; `current_url` its effective URL.
    RedirectDecision Evaluate(const RequestResult& result, const std::string& current_url,
        quicx::HttpMethod method);

private:
    bool follow_;
    int max_redirects_;
    int hops_used_ = 0;
};

#endif  // TOOL_QC_FEATURES_REDIRECT_H
