#include "app/request_driver.h"

#include <chrono>

#include <quicx/http3/if_client.h>
#include <quicx/http3/if_response.h>

#include "app/request_builder.h"

namespace {

std::string Lower(const std::string& s) {
    std::string out = s;
    for (auto& c : out)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return out;
}

}  // namespace

std::string RequestResult::Header(const std::string& name) const {
    std::string want = Lower(name);
    for (const auto& h : headers)
        if (Lower(h.first) == want) return h.second;
    return "";
}

RequestDriver::RequestDriver(ResponseSink& sink, Progress* progress, const Tracer& tracer)
    : sink_(sink), progress_(progress), tracer_(tracer) {}

RequestResult RequestDriver::Run(quicx::IClient& client, const Params& params) {
    {
        std::lock_guard<std::mutex> lock(mtx_);
        done_ = false;
        discard_body_ = false;
        fail_floor_ = params.fail_floor;
        drop_redirect_bodies_ = params.drop_redirect_bodies;
        method_ = params.method;
        result_ = RequestResult{};
        result_.timing.Start();
    }

    tracer_.Info("Trying %s (%s)...", params.url.c_str(), MethodToString(params.method).c_str());

    // Connection-level failures (TLS verify, refused, timeout) are reported
    // through the client-level error handler, not the stream handler.
    // Re-route to this request so Run() cannot hang on connect failures.
    client.SetErrorHandler([self = shared_from_this()](const std::string&, uint32_t error_code) {
        self->OnError(error_code);
    });

    if (!client.DoRequest(params.url, params.method, params.request, shared_from_this())) {
        std::lock_guard<std::mutex> lock(mtx_);
        result_.send_failed = true;
        done_ = true;
        return result_;
    }

    bool completed = false;
    {
        std::unique_lock<std::mutex> lock(mtx_);
        if (params.max_time_s > 0) {
            completed = cv_.wait_for(lock, std::chrono::duration<double>(params.max_time_s),
                [this] { return done_; });
            if (!completed) result_.timed_out = true;
        } else {
            cv_.wait(lock, [this] { return done_; });
            completed = true;
        }
    }

    if (!completed) {
        // Ask the stack to tear down; callbacks fired by the close path will
        // finalize state. Give them a short grace window, then proceed
        // regardless (the library drops its shared_ptr reference on stream
        // destruction, so no dangling access is possible afterwards).
        tracer_.Info("Operation timed out, closing connection");
        client.Close();
        std::unique_lock<std::mutex> lock(mtx_);
        cv_.wait_for(lock, std::chrono::seconds(2), [this] { return done_; });
    }

    {
        // Serialize against in-flight callbacks before touching the sink.
        std::lock_guard<std::mutex> lock(mtx_);
        sink_.Close();
        if (progress_) progress_->Finish(result_.body_bytes);
        result_.timing.MarkDone();
    }
    return result_;
}

void RequestDriver::FinishLocked() {
    done_ = true;
    cv_.notify_all();
}

void RequestDriver::OnHeaders(std::shared_ptr<quicx::IResponse> response) {
    std::lock_guard<std::mutex> lock(mtx_);
    if (done_) return;
    result_.status = response->GetStatusCode();
    for (const auto& h : response->GetHeaders()) result_.headers.emplace_back(h.first, h.second);
    result_.timing.MarkFirstByte();

    // --fail: once the status is known to be >= floor, stop emitting body
    if (fail_floor_ > 0 && result_.status >= fail_floor_) discard_body_ = true;
    // -L: intermediate 3xx bodies are dropped like curl does
    if (drop_redirect_bodies_ && result_.status >= 300 && result_.status < 400)
        discard_body_ = true;

    std::string cl = result_.Header("content-length");
    uint64_t expected = 0;
    if (!cl.empty()) {
        try {
            expected = std::stoull(cl);
        } catch (...) {
            expected = 0;
        }
    }

    if (!discard_body_) {
        if (!sink_.Open(result_.status, result_.headers)) {
            result_.write_failed = true;
            tracer_.Error("failed to open output destination");
        } else {
            sink_.SetExpectedSize(expected);
        }
    }
    if (progress_) progress_->Begin(expected);
    tracer_.Info("Response headers: HTTP/3 %u (%llu bytes declared)", result_.status,
        static_cast<unsigned long long>(expected));

    // RFC 9110 §6.3: HEAD, 1xx, 204 and 304 responses carry no body frames.
    // The stack never fires OnBodyChunk(is_last=true) for them, so waiting
    // would hang until --max-time. Complete the request straight away.
    if (method_ == quicx::HttpMethod::kHead || result_.status < 200 ||
        result_.status == 204 || result_.status == 304) {
        FinishLocked();
    }
}

void RequestDriver::OnBodyChunk(const uint8_t* data, size_t length, bool is_last) {
    std::lock_guard<std::mutex> lock(mtx_);
    if (done_) return;

    if (!discard_body_ && length > 0) {
        if (!sink_.Write(data, length)) {
            result_.write_failed = true;
            tracer_.Error("body write failed");
            FinishLocked();
            return;
        }
    }
    result_.body_bytes += length;
    if (progress_) progress_->Update(result_.body_bytes);
    if (is_last) FinishLocked();
}

void RequestDriver::OnError(uint32_t error_code) {
    std::lock_guard<std::mutex> lock(mtx_);
    if (done_) return;
    result_.error = error_code;
    tracer_.Info("Stream error: %u", error_code);
    FinishLocked();
}
