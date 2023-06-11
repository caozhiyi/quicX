#ifndef QUIC_FRAME_NEW_TOKEN_FRAME
#define QUIC_FRAME_NEW_TOKEN_FRAME

#include <cstdint>
#include "quic/frame/frame_interface.h"

namespace quicx {

class Buffer;
class NewTokenFrame:
    public IFrame {
public:
    NewTokenFrame();
    ~NewTokenFrame();

    virtual bool Encode(std::shared_ptr<IBufferWrite> buffer);
    virtual bool Decode(std::shared_ptr<IBufferRead> buffer, bool with_type = false);
    virtual uint32_t EncodeSize();


    void SetToken(uint8_t* token, uint32_t token_length) { 
        _token = token;
        _token_length = token_length;
    }
    uint8_t* GetToken() { return _token; }
    uint32_t GetTokenLength() { return _token_length; }

private:
    uint32_t _token_length;  // the length of the token in bytes.
    uint8_t* _token;   // An opaque blob that the client may use with a future Initial packet.
};

}

#endif