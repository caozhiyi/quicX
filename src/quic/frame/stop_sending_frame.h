#ifndef QUIC_FRAME_STOP_SENDiNG_FRAME
#define QUIC_FRAME_STOP_SENDiNG_FRAME

#include <cstdint>
#include "frame_interface.h"

namespace quicx {

class StopSendingFrame: public IFrame {
public:
    StopSendingFrame();
    ~StopSendingFrame();

    virtual bool Encode(std::shared_ptr<IBufferWrite> buffer);
    virtual bool Decode(std::shared_ptr<IBufferRead> buffer, bool with_type = false);
    virtual uint32_t EncodeSize();

    void SetStreamID(uint64_t id) { _stream_id = id; }
    uint64_t GetStreamID() { return _stream_id; }

    void SetAppErrorCode(uint64_t err_code) { _app_error_code = err_code; }
    uint64_t GetAppErrorCode() { return _app_error_code; }

private:
    uint64_t _stream_id;      // the Stream ID of the stream being ignored.
    uint64_t _app_error_code; // the application protocol error code.
};

}

#endif