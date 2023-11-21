#ifndef QUIC_FRAME_STREAM_FRAME
#define QUIC_FRAME_STREAM_FRAME

#include <memory>
#include <cstdint>
#include "quic/frame/stream_frame_interface.h"

namespace quicx {
namespace quic {

enum StreamFrameFlag {
    SFF_FIN  = 0x01,
    SFF_LEN  = 0x02,
    SFF_OFF  = 0x04,
    SFF_MASK = 0x07,
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

    bool HasOffset() { return _frame_type & SFF_OFF; }
    void SetOffset(uint64_t offset);
    uint64_t GetOffset() { return _offset; }

    void SetFin() { _frame_type |= SFF_FIN; }
    bool IsFin() { return _frame_type & SFF_FIN; }

    bool HasLength() { return _frame_type & SFF_LEN; }
    void SetData(uint8_t* data, uint32_t send_len);
    uint8_t* GetData() { return _data; }
    uint32_t GetLength() { return _length; }

    static bool IsStreamFrame(uint16_t frame_type);

private:
    uint64_t _offset;     // the byte offset in the stream for the data in this STREAM frame.

    uint32_t _length;     // the length of the Stream Data field in this STREAM frame.
    uint8_t* _data;       // the bytes from the designated stream to be delivered.
};

}
}

#endif