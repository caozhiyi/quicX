#ifndef QUIC_FRAME_NEW_TOKEN_FRAME
#define QUIC_FRAME_NEW_TOKEN_FRAME

#include <cstdint>
#include "quic/frame/if_frame.h"

namespace quicx {
namespace quic {

class Buffer;
class NewTokenFrame:
    public IFrame {
public:
    NewTokenFrame();
    ~NewTokenFrame();

    virtual bool Encode(std::shared_ptr<common::IBuffer> buffer);
    virtual bool Decode(std::shared_ptr<common::IBuffer> buffer, bool with_type = false);
    virtual uint32_t EncodeSize();


    void SetToken(uint8_t* token, uint32_t token_length) { 
        token_ = token;
        token_length_ = token_length;
    }
    uint8_t* GetToken() { return token_; }
    uint32_t GetTokenLength() { return token_length_; }

private:
    uint32_t token_length_;  // the length of the token in bytes.
    uint8_t* token_;   // An opaque blob that the client may use with a future Initial packet. TODO: use shared buffer span
};

}
}

#endif