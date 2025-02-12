#ifndef QUIC_FRAME_STREAM_FRAME
#define QUIC_FRAME_STREAM_FRAME

#include <memory>
#include <cstdint>
#include "quic/frame/if_stream_frame.h"

namespace quicx {
namespace quic {

enum StreamFrameFlag: uint8_t {
    kFinFlag  = 0x01,
    kLenFlag  = 0x02,
    kOffFlag  = 0x04,
    kMaskFlag = 0x07,
};

class Buffer;
class StreamFrame:
    public IStreamFrame {
public:
    StreamFrame();
    StreamFrame(uint16_t frame_type);
    ~StreamFrame();

    virtual bool Encode(std::shared_ptr<common::IBufferWrite> buffer);
    virtual bool Decode(std::shared_ptr<common::IBufferRead> buffer, bool with_type = false);
    virtual uint32_t EncodeSize();

    bool HasOffset() { return frame_type_ & kOffFlag; }
    void SetOffset(uint64_t offset);
    uint64_t GetOffset() { return offset_; }

    void SetFin() { frame_type_ |= kFinFlag; }
    bool IsFin() { return frame_type_ & kFinFlag; }

    bool HasLength() { return frame_type_ & kLenFlag; }
    void SetData(uint8_t* data, uint32_t send_len);
    uint8_t* GetData() { return data_; }
    uint32_t GetLength() { return length_; }

    static bool IsStreamFrame(uint16_t frame_type);

private:
    uint64_t offset_;     // the byte offset in the stream for the data in this STREAM frame.

    uint32_t length_;     // the length of the Stream Data field in this STREAM frame.
    uint8_t* data_;       // the bytes from the designated stream to be delivered.
};

}
}

#endif