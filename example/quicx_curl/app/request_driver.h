#ifndef TOOL_QC_APP_REQUEST_DRIVER_H
#define TOOL_QC_APP_REQUEST_DRIVER_H

#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include <quicx/http3/if_async_handler.h>
#include <quicx/http3/type.h>

#include "io/progress.h"
#include "io/sink.h"
#include "obs/timing.h"
#include "obs/tracer.h"

namespace quicx {
class IClient;
}

// Outcome of a single request (one hop, redirects handled by orchestrator).
struct RequestResult {
    uint32_t status = 0;                 // HTTP status, 0 if no response
    uint32_t error = 0;                  // protocol/network error from OnError
    bool send_failed = false;            // DoRequest() returned false
    bool timed_out = false;              // --max-time hit
    bool write_failed = false;           // sink write error (curl exit 23)
    uint64_t body_bytes = 0;
    std::vector<std::pair<std::string, std::string>> headers;
    Timing timing;

    bool Ok() const { return error == 0 && !send_failed && !timed_out && !write_failed; }
    std::string Header(const std::string& name) const;
};

// Streaming state machine bridging the library's worker-thread callbacks to
// the main-thread Run() call. One instance per request. The library keeps a
// shared_ptr to this object for the lifetime of the stream, so callbacks
// never dangle even when Run() returns early on timeout.
class RequestDriver : public quicx::IAsyncClientHandler,
                      public std::enable_shared_from_this<RequestDriver> {
public:
    struct Params {
        std::string url;
        quicx::HttpMethod method = quicx::HttpMethod::kGet;
        std::shared_ptr<quicx::IRequest> request;
        double max_time_s = 0;       // 0 = unlimited
        uint32_t fail_floor = 0;     // --fail: discard body when status >= floor
        bool drop_redirect_bodies = false;  // -L: discard 3xx bodies (curl behavior)
    };

    RequestDriver(ResponseSink& sink, Progress* progress, const Tracer& tracer);

    // Blocking call.
    RequestResult Run(quicx::IClient& client, const Params& params);

    // IAsyncClientHandler (worker thread)
    void OnHeaders(std::shared_ptr<quicx::IResponse> response) override;
    void OnBodyChunk(const uint8_t* data, size_t length, bool is_last) override;
    void OnError(uint32_t error_code) override;

private:
    void FinishLocked();  // requires mtx_ held; notifies cv_

    ResponseSink& sink_;
    Progress* progress_;
    Tracer tracer_;

    std::mutex mtx_;
    std::condition_variable cv_;
    bool done_ = false;
    bool discard_body_ = false;
    uint32_t fail_floor_ = 0;
    bool drop_redirect_bodies_ = false;
    quicx::HttpMethod method_ = quicx::HttpMethod::kGet;  // HEAD => no body frames

    // written under mtx_, read by Run() after completion
    RequestResult result_;
};

#endif  // TOOL_QC_APP_REQUEST_DRIVER_H
