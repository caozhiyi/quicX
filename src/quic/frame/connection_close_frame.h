#ifndef QUIC_FRAME_CONNECTION_CLOSE_FRAME
#define QUIC_FRAME_CONNECTION_CLOSE_FRAME

#include <string>
#include <cstdint>
#include "quic/frame/if_frame.h"

namespace quicx {
namespace quic {

class ConnectionCloseFrame:
    public IFrame {
public:
    ConnectionCloseFrame();
    ConnectionCloseFrame(uint16_t frame_type);
    ~ConnectionCloseFrame();

    virtual bool Encode(std::shared_ptr<common::IBuffer> buffer);
    virtual bool Decode(std::shared_ptr<common::IBuffer> buffer, bool with_type = false);
    virtual uint32_t EncodeSize();

    void SetErrorCode(uint32_t error_code) { error_code_ = error_code; }
    uint32_t GetErrorCode() { return error_code_; }

    void SetErrFrameType(uint32_t frame_type) { err_frame_type_ = frame_type; }
    uint32_t GetErrFrameType() { return err_frame_type_; }

    void SetReason(const std::string& reason) { reason_ = reason; }
    const std::string& GetReason() { return reason_; }

private:
    bool is_application_error_;
    uint32_t error_code_;        // indicates the reason for closing this connection.
    uint32_t err_frame_type_;    // the type of frame that triggered the error.
    std::string reason_;
    /*
    uint32_t reason_length; // the length of the reason phrase in bytes.
    char* reason_;           // why the connection was closed.
    */
};

}
}

#endif