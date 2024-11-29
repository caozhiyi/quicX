#ifndef QUIC_FRAME_STOP_SENDiNG_FRAME
#define QUIC_FRAME_STOP_SENDiNG_FRAME

#include <cstdint>
#include "quic/frame/if_stream_frame.h"

namespace quicx {
namespace quic {

class StopSendingFrame:
    public IStreamFrame {
public:
    StopSendingFrame();
    ~StopSendingFrame();

    virtual bool Encode(std::shared_ptr<common::IBufferWrite> buffer);
    virtual bool Decode(std::shared_ptr<common::IBufferRead> buffer, bool with_type = false);
    virtual uint32_t EncodeSize();

    void SetAppErrorCode(uint64_t err_code) { app_error_code_ = err_code; }
    uint64_t GetAppErrorCode() { return app_error_code_; }

private:
    uint64_t app_error_code_; // the application protocol error code.
};

}
}

#endif