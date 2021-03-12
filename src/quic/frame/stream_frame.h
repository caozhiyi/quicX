#ifndef QUIC_FRAME_STREAM_FRAME
#define QUIC_FRAME_STREAM_FRAME

#include <memory>
#include <cstdint>
#include "frame_interface.h"

namespace quicx {

class Buffer;
class StreamFrame: public Frame {
public:
    enum StreamFrameFlag {
        SFF_OFF = 0x04,
        SFF_LEN = 0x02,
        SFF_FIN = 0x01
    };

    StreamFrame();
    ~StreamFrame();

    bool Encode(std::shared_ptr<Buffer> buffer, std::shared_ptr<AlloterWrap> alloter);
    bool Decode(std::shared_ptr<Buffer> buffer, std::shared_ptr<AlloterWrap> alloter, bool with_type);
    uint32_t EncodeSize();

    void SetStreamID(uint64_t stream_id) { _stream_id = stream_id; }
    uint64_t GetStreamID() { return _stream_id; }

    bool HasOffset() { return _frame_type & SFF_OFF; }
    void SetOffset(uint64_t offset);
    uint64_t GetOffset() { return _offset; }

    void SetFin() { _frame_type |= SFF_FIN; }
    bool IsFin() { return _frame_type & SFF_FIN; }

    bool HasLength() { return _frame_type & SFF_LEN; }
    void SetData(std::shared_ptr<Buffer> data);
    std::shared_ptr<Buffer> GetData() { return _data; }

private:
    uint64_t _stream_id;  // indicating the stream ID of the stream.
    uint64_t _offset;     // the byte offset in the stream for the data in this STREAM frame.

    std::shared_ptr<Buffer> _data;
    /*
    uint32_t _lentgh;     // the length of the Stream Data field in this STREAM frame.
    char* _data;          // the bytes from the designated stream to be delivered.
    */
};

}

#endif