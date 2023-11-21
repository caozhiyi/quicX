#ifndef QUIC_FRAME_STOP_SENDiNG_FRAME
#define QUIC_FRAME_STOP_SENDiNG_FRAME

#include <cstdint>
#include "quic/frame/stream_frame_interface.h"

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

    void SetAppErrorCode(uint64_t err_code) { _app_error_code = err_code; }
    uint64_t GetAppErrorCode() { return _app_error_code; }

private:
    uint64_t _app_error_code; // the application protocol error code.
};

}
}

#endif