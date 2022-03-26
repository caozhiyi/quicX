#ifndef QUIC_FRAME_STREAM_FRAME
#define QUIC_FRAME_STREAM_FRAME

#include <memory>
#include <cstdint>
#include "frame_interface.h"

namespace quicx {

enum StreamFrameFlag {
    SFF_OFF = 0x04,
    SFF_LEN = 0x02,
    SFF_FIN = 0x01
};

class Buffer;
class StreamFrame: public IFrame {
public:
    StreamFrame();
    StreamFrame(uint16_t frame_type);
    ~StreamFrame();

    virtual bool Encode(std::shared_ptr<IBufferWriteOnly> buffer);
    virtual bool Decode(std::shared_ptr<IBufferReadOnly> buffer, bool with_type = false);
    virtual uint32_t EncodeSize();

    void SetStreamID(uint64_t stream_id) { _stream_id = stream_id; }
    uint64_t GetStreamID() { return _stream_id; }

    bool HasOffset() { return _frame_type & SFF_OFF; }
    void SetOffset(uint64_t offset);
    uint64_t GetOffset() { return _offset; }

    void SetFin() { _frame_type |= SFF_FIN; }
    bool IsFin() { return _frame_type & SFF_FIN; }

    bool HasLength() { return _frame_type & SFF_LEN; }
    void SetData(char* data, uint32_t send_len);
    char* GetData() { return _data; }
    uint32_t GetLength() { return _length; }

private:
    uint64_t _stream_id;  // indicating the stream ID of the stream.
    uint64_t _offset;     // the byte offset in the stream for the data in this STREAM frame.

    uint32_t _length;     // the length of the Stream Data field in this STREAM frame.
    char* _data;          // the bytes from the designated stream to be delivered.
};

}

#endif