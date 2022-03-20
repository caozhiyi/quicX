#ifndef QUIC_FRAME_NEW_TOKEN_FRAME
#define QUIC_FRAME_NEW_TOKEN_FRAME

#include <cstdint>
#include "frame_interface.h"

namespace quicx {

class Buffer;
class NewTokenFrame: public IFrame {
public:
    NewTokenFrame();
    ~NewTokenFrame();

    virtual bool Encode(std::shared_ptr<IBufferWriteOnly> buffer);
    virtual bool Decode(std::shared_ptr<IBufferReadOnly> buffer, bool with_type = false);
    virtual uint32_t EncodeSize();


    void SetToken(char* token, uint32_t token_length) { 
        _token = token;
        _token_length = token_length;
    }
    char* GetToken() { return _token; }

private:
    uint32_t _token_length;  // the length of the token in bytes.
    char* _token;            // An opaque blob that the client may use with a future Initial packet.
};

}

#endif