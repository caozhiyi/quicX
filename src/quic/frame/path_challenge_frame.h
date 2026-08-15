#ifndef QUIC_FRAME_PATH_CHALLENGE_FRAME
#define QUIC_FRAME_PATH_CHALLENGE_FRAME

#include "quic/frame/if_frame.h"

namespace quicx {
namespace quic {

static const uint16_t kPathDataLength = 8;

class PathResponseFrame;
class PathChallengeFrame: public IFrame {
public:
    PathChallengeFrame();
    ~PathChallengeFrame();

    virtual bool Encode(std::shared_ptr<common::IBuffer> buffer);
    virtual bool Decode(std::shared_ptr<common::IBuffer> buffer, bool with_type = false);
    virtual uint32_t EncodeSize();

    // Constant-time comparison against the peer's PATH_RESPONSE token.
    bool CompareData(std::shared_ptr<PathResponseFrame> response);

    // Fills data_ with 8 bytes from the cryptographic RNG. RFC 9000 section 8.2.1
    // requires the payload to be hard to guess, so this must never fall back to a
    // predictable PRNG. Returns false if the CSPRNG failed, in which case data_ is
    // left zeroed and the caller must abort the path probe.
    bool MakeData();
    uint8_t* GetData() { return data_; }

private:
    uint8_t data_[kPathDataLength];  // 8-byte field contains arbitrary data.
};

}  // namespace quic
}  // namespace quicx

#endif