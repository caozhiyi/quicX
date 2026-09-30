#ifndef TOOL_QC_IO_SINK_H
#define TOOL_QC_IO_SINK_H

#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <utility>
#include <vector>

// ============================================================================
// ResponseSink: the single output abstraction for response payloads.
// Body bytes go through Write() only (binary-safe, no newline munging).
// Progress/writeout derive their counters from BytesWritten().
// ============================================================================

class ResponseSink {
public:
    virtual ~ResponseSink() = default;

    // Called once when response headers arrive (before first body chunk).
    // Returns false if the destination could not be opened (e.g. bad path).
    virtual bool Open(uint32_t status, const std::vector<std::pair<std::string, std::string>>& headers) = 0;

    // Binary-safe body output. Returns false on write failure.
    virtual bool Write(const uint8_t* data, size_t len) = 0;

    // Flush / close. Called exactly once when the response ends (or on abort).
    virtual void Close() = 0;

    virtual uint64_t BytesWritten() const = 0;

    // Content-Length advertised by the server, 0 when unknown. Set by owner
    // between Open() and first Write() so progress can show a total.
    virtual void SetExpectedSize(uint64_t size) { (void)size; }
    virtual uint64_t ExpectedSize() const { return 0; }
};

// Decorator: prints "HTTP/3 <status>" + headers before forwarding body to the
// wrapped sink (implements curl -i against any destination).
class HeaderDecorator : public ResponseSink {
public:
    explicit HeaderDecorator(std::unique_ptr<ResponseSink> inner) : inner_(std::move(inner)) {}

    bool Open(uint32_t status, const std::vector<std::pair<std::string, std::string>>& headers) override;
    bool Write(const uint8_t* data, size_t len) override { return inner_->Write(data, len); }
    void Close() override { inner_->Close(); }
    uint64_t BytesWritten() const override { return inner_->BytesWritten(); }
    void SetExpectedSize(uint64_t size) override { inner_->SetExpectedSize(size); }
    uint64_t ExpectedSize() const override { return inner_->ExpectedSize(); }

private:
    std::unique_ptr<ResponseSink> inner_;
};

// Discards all body bytes (used for redirect hops, -f failures, /dev/null).
class DiscardSink : public ResponseSink {
public:
    bool Open(uint32_t, const std::vector<std::pair<std::string, std::string>>&) override { return true; }
    bool Write(const uint8_t*, size_t len) override { bytes_ += len; return true; }
    void Close() override {}
    uint64_t BytesWritten() const override { return bytes_; }

private:
    uint64_t bytes_ = 0;
};

// Factory: builds the sink chain for one transfer according to output options.
// `path` is "-" for stdout or a file path (already resolved per-URL).
std::unique_ptr<ResponseSink> MakeBodySink(const std::string& path, bool include_headers);

#endif  // TOOL_QC_IO_SINK_H
