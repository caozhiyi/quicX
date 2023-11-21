#ifndef QUIC_FRAME_CONNECTION_CLOSE_FRAME
#define QUIC_FRAME_CONNECTION_CLOSE_FRAME

#include <string>
#include <cstdint>
#include "quic/frame/frame_interface.h"

namespace quicx {
namespace quic {

class ConnectionCloseFrame:
    public IFrame {
public:
    ConnectionCloseFrame();
    ConnectionCloseFrame(uint16_t frame_type);
    ~ConnectionCloseFrame();

    virtual bool Encode(std::shared_ptr<common::IBufferWrite> buffer);
    virtual bool Decode(std::shared_ptr<common::IBufferRead> buffer, bool with_type = false);
    virtual uint32_t EncodeSize();

    void SetErrorCode(uint32_t error_code) { _error_code = error_code; }
    uint32_t GetErrorCode() { return _error_code; }

    void SetErrFrameType(uint32_t frame_type) { _err_frame_type = frame_type; }
    uint32_t GetErrFrameType() { return _err_frame_type; }

    void SetReason(const std::string& reason) { _reason = reason; }
    const std::string& GetReason() { return _reason; }

private:
    bool _is_application_error;
    uint32_t _error_code;        // indicates the reason for closing this connection.
    uint32_t _err_frame_type;    // the type of frame that triggered the error.
    std::string _reason;
    /*
    uint32_t _reason_length; // the length of the reason phrase in bytes.
    char* _reason;           // why the connection was closed.
    */
};

}
}

#endif