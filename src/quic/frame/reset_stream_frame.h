#ifndef QUIC_FRAME_RESET_STREAM_FRAME
#define QUIC_FRAME_RESET_STREAM_FRAME

#include <cstdint>
#include "quic/frame/if_stream_frame.h"

namespace quicx {
namespace quic {

class ResetStreamFrame:
    public IStreamFrame {
public:
    ResetStreamFrame();
    ~ResetStreamFrame();

    virtual bool Encode(std::shared_ptr<common::IBuffer> buffer);
    virtual bool Decode(std::shared_ptr<common::IBuffer> buffer, bool with_type = false);
    virtual uint32_t EncodeSize();

    void SetAppErrorCode(uint64_t err_code) { app_error_code_ = err_code; }
    uint64_t GetAppErrorCode() { return app_error_code_; }

    void SetFinalSize(uint64_t size) { final_size_ = size; }
    uint64_t GetFinalSize() { return final_size_; }

private:
   uint64_t app_error_code_; // the application protocol error code.
   uint64_t final_size_;     // the final size of the stream by the RESET_STREAM sender.
}; 


}
}

#endif