#ifndef QUIC_STREAM_RECV_STREAM
#define QUIC_STREAM_RECV_STREAM

#include <string>
#include "quic/stream/stream_interface.h"

namespace quicx {

class AlloterWrap;
class BlockMemoryPool;
class SortBufferQueue;
class RecvStream: public Stream {
public:
    RecvStream(StreamType type);
    ~RecvStream();

    // abort reading
    void Close();

    void HandleFrame(std::shared_ptr<IFrame> frame);

    void SetReadCallBack(StreamReadBack rb) { _read_back = rb; }

    void SetDataLimit(uint32_t limit);
    uint32_t GetDataLimit() { return _data_limit; }

    void SetToDataMax(uint32_t to_data_max) { _to_data_max = to_data_max; }
    uint32_t GetToDataMax() { return _to_data_max; }

private:
    void HandleStreamFrame(std::shared_ptr<IFrame> frame);
    void HandleStreamDataBlockFrame(std::shared_ptr<IFrame> frame);
    void HandleResetStreamFrame(std::shared_ptr<IFrame> frame);

private:
    uint32_t _data_limit;  // peer send data limit
    uint32_t _to_data_max; // if recv data more than this value, send max stream data frame
    uint64_t _final_offset;

    StreamReadBack _read_back;
    std::shared_ptr<SortBufferQueue> _buffer;

    std::shared_ptr<AlloterWrap> _alloter;
    std::shared_ptr<BlockMemoryPool> _block_pool;
};

}

#endif
