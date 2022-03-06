#ifndef QUIC_STREAM_SEND_STREAM
#define QUIC_STREAM_SEND_STREAM

#include <string>
#include "quic/stream/stream_interface.h"

namespace quicx {

class AlloterWrap;
class BlockMemoryPool;
class SendStream: public Stream {
public:
    SendStream(StreamType type);
    virtual ~SendStream();

    // write data to stream
    int32_t Write(std::shared_ptr<Buffer> buffer, uint32_t len = 0);
    int32_t Write(const std::string &data);
    int32_t Write(char* data, uint32_t len);
 
    // end the stream
    void Close();

    void HandleFrame(std::shared_ptr<Frame> frame);

    // reset the stream
    void Reset(uint64_t err);

    void SetWriteCallBack(StreamWriteBack wb) { _write_back = wb; }

private:
    void HandleMaxStreamDataFrame(std::shared_ptr<Frame> frame);
    void HandleStopSendingFrame(std::shared_ptr<Frame> frame);

private:
    uint64_t _data_offset;
    uint64_t _peer_data_limit;
    StreamWriteBack _write_back;

    std::shared_ptr<AlloterWrap> _alloter;
    std::shared_ptr<BlockMemoryPool> _block_pool;
};

}

#endif
