#include "app/orchestrator.h"

#include <thread>

#include <quicx/http3/if_client.h>
#include <quicx/http3/if_request.h>

#include "app/request_builder.h"
#include "app/url.h"
#include "features/cookie_jar.h"
#include "features/migration_ctl.h"
#include "features/push_display.h"
#include "features/redirect.h"
#include "io/progress.h"
#include "io/sink.h"
#include "io/writeout.h"
#include "obs/stats_report.h"
#include "session/session.h"

namespace {

// curl exit codes used by this tool
enum ExitCode {
    kOk = 0,
    kUnsupportedProtocol = 1,
    kUrlMalformat = 3,
    kCouldNotConnect = 7,
    kHttpReturnedError = 22,  // --fail and HTTP >= 400
    kWriteError = 23,
    kReadError = 26,
    kOperationTimedOut = 28,
};

int MapResultToExit(const RequestResult& r, const OutputOptions& out) {
    if (r.timed_out) return kOperationTimedOut;
    if (r.write_failed) return kWriteError;
    if (r.send_failed || r.error != 0) return kCouldNotConnect;
    if (out.fail && r.status >= 400) return kHttpReturnedError;
    return kOk;
}

}  // namespace

Orchestrator::Orchestrator(const Options& opts)
    : opts_(opts), tracer_(opts.out.verbose, opts.out.show_error || !opts.out.silent) {
    jar_ = std::make_unique<CookieJar>();
    if (!opts_.req.cookie_read_file.empty()) jar_->LoadFromSpec(opts_.req.cookie_read_file);
}

Orchestrator::~Orchestrator() = default;

Orchestrator::PerUrlPlan Orchestrator::ResolveTarget(size_t url_index, const std::string& url) const {
    PerUrlPlan plan;
    if (!opts_.out.output_files.empty()) {
        // curl semantics: a single -o applies to every URL; multiple -o map
        // to URLs by index (extra URLs fall back to stdout).
        if (opts_.out.output_files.size() == 1) plan.target_path = opts_.out.output_files[0];
        else if (url_index < opts_.out.output_files.size()) plan.target_path = opts_.out.output_files[url_index];
    } else if (opts_.out.remote_name) {
        ParsedUrl parsed;
        if (ParseUrl(url, parsed)) {
            std::string name = parsed.path.substr(parsed.path.find_last_of('/') + 1);
            plan.target_path = name.empty() ? "download" : name;
        }
    }
    return plan;
}

int Orchestrator::RunOneUrl(ClientSession& session, const PerUrlPlan& plan, const std::string& url,
    RequestResult& last_out, int& hops_out, std::string& effective_url_out) {
    quicx::IClient* client = session.Get();
    if (!client) return kCouldNotConnect;
    effective_url_out = url;

    RedirectPolicy redirect(opts_.req.follow_redirect, opts_.req.max_redirects);
    std::string current_url = url;
    quicx::HttpMethod method = MethodFromString(opts_.req.method);
    if (opts_.req.head) method = quicx::HttpMethod::kHead;
    // -T implies PUT unless -X pinned (curl semantics; "GET" is the default)
    else if (!opts_.req.upload_file.empty() && opts_.req.method == "GET")
        method = quicx::HttpMethod::kPut;

    // Mid-transfer migration demo: arm helper thread before the request.
    std::thread delayed_migration;
    if (opts_.tp.migrate && opts_.tp.migrate_delay_ms > 0) {
        delayed_migration = std::thread([&client, tp = opts_.tp, tr = tracer_]() {
            MigrationCtl(tp, tr).TriggerDelayed(*client);
        });
    }

    // One sink serves the whole hop chain (FileSink::Open is idempotent).
    // -I implies showing the response headers, as curl does (a HEAD response
    // has no body, so without this the command would print nothing at all).
    bool include_headers = opts_.out.include_headers || opts_.req.head;
    std::unique_ptr<ResponseSink> sink = MakeBodySink(plan.target_path, include_headers);
    Progress progress(opts_.out.progress_bar && !opts_.out.silent);

    int exit_code = kOk;
    while (true) {
        auto request = BuildRequest(opts_.req, jar_.get(), current_url);
        if (!request) {
            tracer_.Error("failed to build request (unreadable body/upload file?)");
            exit_code = kReadError;
            break;
        }

        RequestDriver::Params params;
        params.url = current_url;
        params.method = method;
        params.max_time_s = opts_.req.max_time_s;
        params.fail_floor = opts_.out.fail ? 400 : 0;
        params.drop_redirect_bodies = opts_.req.follow_redirect;
        params.request = request;

        auto driver = std::make_shared<RequestDriver>(*sink, &progress, tracer_);
        RequestResult result = driver->Run(*client, params);
        last_out = result;

        jar_->AbsorbResponse(result.headers, current_url);

        if (!result.Ok()) {
            exit_code = MapResultToExit(result, opts_.out);
            if (result.timed_out)
                tracer_.Error("operation timed out after %.0f s", opts_.req.max_time_s);
            else if (result.error != 0)
                tracer_.Error("stream failed with error %u", result.error);
            break;
        }
        if (opts_.out.fail && result.status >= 400) {
            tracer_.Error("HTTP %u returned, failing silently (--fail)", result.status);
            exit_code = kHttpReturnedError;
            break;
        }

        RedirectDecision next = redirect.Evaluate(result, current_url, method);
        if (!next.follow) {
            effective_url_out = current_url;
            break;
        }

        tracer_.Info("Redirect (%u) -> %s", result.status, next.next_url.c_str());
        if (next.switch_to_get) method = quicx::HttpMethod::kGet;
        current_url = next.next_url;
    }
    hops_out = redirect.hops_used();

    if (delayed_migration.joinable()) delayed_migration.join();

    // Post-transfer migration demo (default --migrate form)
    if (opts_.tp.migrate && opts_.tp.migrate_delay_ms == 0 && session.Get())
        MigrationCtl(opts_.tp, tracer_).TriggerNow(*session.Get());
    return exit_code;
}

int Orchestrator::Run(ClientSession& session) {
    if (opts_.tp.migrate) MigrationCtl::InstallCallback(*session.Get(), tracer_);
    if (opts_.tp.push) InstallPushHandlers(*session.Get(), tracer_);

    int worst = kOk;
    for (size_t i = 0; i < opts_.req.urls.size(); ++i) {
        const std::string& url = opts_.req.urls[i];
        ParsedUrl parsed;
        if (!ParseUrl(url, parsed)) {
            tracer_.Error("malformed URL: %s", url.c_str());
            if (worst == kOk) worst = kUrlMalformat;
            continue;
        }
        // HTTP/3 runs over QUIC, which mandates TLS: plain http:// has no
        // meaningful mapping here, so it is rejected outright (curl exit 1)
        // instead of silently being upgraded to https.
        if (parsed.scheme != "https") {
            tracer_.Error("scheme '%s' not supported (HTTP/3 requires https)",
                parsed.scheme.c_str());
            if (worst == kOk) worst = kUnsupportedProtocol;
            continue;
        }

        RequestResult last;
        int hops = 0;
        std::string effective_url;
        int code = RunOneUrl(session, ResolveTarget(i, url), url, last, hops, effective_url);
        if (code != kOk && worst == kOk) worst = code;
        EmitWriteoutAndStats(url, effective_url, last, hops);
    }

    if (!opts_.req.cookie_write_file.empty() && !jar_->SaveTo(opts_.req.cookie_write_file))
        tracer_.Error("failed to write cookie jar %s", opts_.req.cookie_write_file.c_str());

    session.Close();
    return worst;
}

void Orchestrator::EmitWriteoutAndStats(const std::string& original_url,
    const std::string& effective_url, const RequestResult& last, int hops) {
    if (opts_.out.stats) {
        StatsInput s;
        s.timing = &last.timing;
        s.bytes_downloaded = last.body_bytes;
        s.status_code = last.status;
        s.redirect_hops = hops;
        s.quic_version = opts_.tp.quic_version;
        s.zero_rtt_requested = opts_.tp.zero_rtt;
        PrintStats(stderr, s);
    }
    if (!opts_.out.write_out.empty()) {
        WriteoutContext wctx;
        wctx.url_original = original_url;
        wctx.url_effective = effective_url;
        wctx.http_code = last.status;
        wctx.size_download = last.body_bytes;
        wctx.time_total = last.timing.TotalSeconds();
        wctx.time_starttransfer = last.timing.TtfbSeconds();
        double total = last.timing.TotalSeconds();
        wctx.speed_download = total > 0 ? static_cast<double>(last.body_bytes) / total : 0;
        wctx.num_redirects = hops;
        wctx.content_type = last.Header("content-type");
        if (!last.Ok()) wctx.errormsg = last.timed_out ? "operation timed out" : "transfer failed";
        wctx.quic_version = opts_.tp.quic_version;
        WriteOut(opts_.out.write_out, wctx, stdout);
    }
}
