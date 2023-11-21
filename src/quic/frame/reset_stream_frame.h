#ifndef QUIC_FRAME_RESET_STREAM_FRAME
#define QUIC_FRAME_RESET_STREAM_FRAME

#include <cstdint>
#include "quic/frame/stream_frame_interface.h"

namespace quicx {
namespace quic {

class ResetStreamFrame:
    public IStreamFrame {
public:
    ResetStreamFrame();
    ~ResetStreamFrame();

    virtual bool Encode(std::shared_ptr<common::IBufferWrite> buffer);
    virtual bool Decode(std::shared_ptr<common::IBufferRead> buffer, bool with_type = false);
    virtual uint32_t EncodeSize();

    void SetAppErrorCode(uint64_t err_code) { _app_error_code = err_code; }
    uint64_t GetAppErrorCode() { return _app_error_code; }

    void SetFinalSize(uint64_t size) { _final_size = size; }
    uint64_t GetFinalSize() { return _final_size; }

private:
   uint64_t _app_error_code; // the application protocol error code.
   uint64_t _final_size;     // the final size of the stream by the RESET_STREAM sender.
}; 


}
}

#endif