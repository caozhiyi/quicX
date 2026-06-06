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
    // Token bytes. We hold a raw, non-owning pointer here matching the rest of
    // the frame layer (see e.g. CryptoFrame, StreamFrame) — the buffer's
    // lifetime is tied to the receive buffer that produced the frame, and the
    // frame itself is consumed before that buffer is recycled. Migrating the
    // whole frame layer to a span / shared_buffer abstraction is tracked as a
    // post-1.0 cleanup in learning_project_roadmap.md §2.
    uint8_t* token_;
};

}
}

#endif