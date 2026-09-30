#include "io/sink.h"

namespace {

class StdoutSink : public ResponseSink {
public:
    ~StdoutSink() override { Close(); }
    bool Open(uint32_t, const std::vector<std::pair<std::string, std::string>>&) override {
        opened_ = true;
        return true;
    }
    bool Write(const uint8_t* data, size_t len) override {
        if (!opened_) return false;
        if (std::fwrite(data, 1, len, stdout) != len) return false;
        bytes_ += len;
        return true;
    }
    void Close() override {
        if (opened_) std::fflush(stdout);
        opened_ = false;
    }
    uint64_t BytesWritten() const override { return bytes_; }

private:
    bool opened_ = false;
    uint64_t bytes_ = 0;
};

class FileSink : public ResponseSink {
public:
    explicit FileSink(const std::string& path) : path_(path) {}
    ~FileSink() override { Close(); }

    bool Open(uint32_t, const std::vector<std::pair<std::string, std::string>>&) override {
        if (file_) return true;
#ifdef _WIN32
        if (fopen_s(&file_, path_.c_str(), "wb") != 0) file_ = nullptr;
#else
        file_ = std::fopen(path_.c_str(), "wb");
#endif
        return file_ != nullptr;
    }
    bool Write(const uint8_t* data, size_t len) override {
        if (!file_ || std::fwrite(data, 1, len, file_) != len) return false;
        bytes_ += len;
        return true;
    }
    void Close() override {
        if (file_) {
            std::fclose(file_);
            file_ = nullptr;
        }
    }
    uint64_t BytesWritten() const override { return bytes_; }
    void SetExpectedSize(uint64_t size) override { expected_ = size; }
    uint64_t ExpectedSize() const override { return expected_; }

private:
    std::string path_;
    std::FILE* file_ = nullptr;
    uint64_t bytes_ = 0;
    uint64_t expected_ = 0;
};

}  // namespace

bool HeaderDecorator::Open(uint32_t status, const std::vector<std::pair<std::string, std::string>>& headers) {
    if (!inner_->Open(status, headers)) return false;
    std::fprintf(stdout, "HTTP/3 %03u\n", status);
    for (const auto& h : headers) std::fprintf(stdout, "%s: %s\n", h.first.c_str(), h.second.c_str());
    std::fprintf(stdout, "\n");
    return true;
}

std::unique_ptr<ResponseSink> MakeBodySink(const std::string& path, bool include_headers) {
    std::unique_ptr<ResponseSink> inner;
    if (path == "-" || path.empty()) {
        inner = std::make_unique<StdoutSink>();
    } else if (path == "/dev/null") {
        inner = std::make_unique<DiscardSink>();
    } else {
        inner = std::make_unique<FileSink>(path);
    }
    if (include_headers) return std::make_unique<HeaderDecorator>(std::move(inner));
    return inner;
}
