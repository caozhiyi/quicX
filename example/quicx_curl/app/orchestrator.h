#ifndef TOOL_QC_APP_ORCHESTRATOR_H
#define TOOL_QC_APP_ORCHESTRATOR_H

#include <memory>
#include <string>

#include "app/request_driver.h"  // RequestResult
#include "cli/options.h"
#include "obs/tracer.h"

class ClientSession;
class CookieJar;

// Drives the whole operation: URL list, per-URL redirect hops, output
// target resolution, writeout/stats emission, exit-code aggregation.
class Orchestrator {
public:
    Orchestrator(const Options& opts);
    ~Orchestrator();

    Orchestrator(const Orchestrator&) = delete;
    Orchestrator& operator=(const Orchestrator&) = delete;

    // Returns process exit code (curl-compatible semantics).
    int Run(ClientSession& session);

private:
    struct PerUrlPlan {
        std::string target_path = "-";  // sink destination ("-" = stdout)
    };

    int RunOneUrl(ClientSession& session, const PerUrlPlan& plan, const std::string& url,
        RequestResult& last_out, int& hops_out, std::string& effective_url_out);
    PerUrlPlan ResolveTarget(size_t url_index, const std::string& url) const;
    void EmitWriteoutAndStats(const std::string& original_url, const std::string& effective_url,
        const RequestResult& last, int hops);

    Options opts_;
    Tracer tracer_;
    std::unique_ptr<CookieJar> jar_;
};

#endif  // TOOL_QC_APP_ORCHESTRATOR_H
