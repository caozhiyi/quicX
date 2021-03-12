#ifndef QUIC_FRAME_NEW_TOKEN_FRAME
#define QUIC_FRAME_NEW_TOKEN_FRAME

#include <cstdint>
#include "frame_interface.h"

namespace quicx {

class Buffer;
class NewTokenFrame: public Frame {
public:
    NewTokenFrame();
    ~NewTokenFrame();

    bool Encode(std::shared_ptr<Buffer> buffer, std::shared_ptr<AlloterWrap> alloter);
    bool Decode(std::shared_ptr<Buffer> buffer, std::shared_ptr<AlloterWrap> alloter, bool with_type = false);
    uint32_t EncodeSize();

    void SetToken(std::shared_ptr<Buffer> token) { _token = token; }
    std::shared_ptr<Buffer> GetToken() { return _token; }

private:
    std::shared_ptr<Buffer> _token;
    /*
    uint32_t _token_length;  // the length of the token in bytes.
    char* _token;       // An opaque blob that the client may use with a future Initial packet.
    */
};

}

#endif