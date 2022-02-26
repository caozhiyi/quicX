#ifndef QUIC_STREAM_RECV_STREAM
#define QUIC_STREAM_RECV_STREAM

#include <string>
#include "stream_interface.h"
#include "stream_state_machine_interface.h"

namespace quicx {

enum RecvStreamEvent {
    RSE_RECV_ALL_DATA = 0x01,
    RSE_READ_ALL_DATA = 0x02,
    RSE_READ_RST      = 0x03,
};

class RecvStreamStateMachine: public StreamStateMachine {
public:
    RecvStreamStateMachine(StreamStatus s = SS_RECV);
    ~RecvStreamStateMachine();

    bool OnFrame(uint16_t frame_type);

    bool OnEvent(RecvStreamEvent event);
};

class AlloterWrap;
class BlockMemoryPool;
class SortBufferQueue;
class RecvStream: public Stream {
public:
    RecvStream(StreamType type);
    ~RecvStream();

    // abort reading
    void Close();

    void HandleFrame(std::shared_ptr<Frame> frame);

    void SetReadCallBack(StreamReadBack rb) { _read_back = rb; }

    void SetDataLimit(uint32_t limit);
    uint32_t GetDataLimit() { return _data_limit; }

    void SetToDataMax(uint32_t to_data_max) { _to_data_max = to_data_max; }
    uint32_t GetToDataMax() { return _to_data_max; }

private:
    void HandleStreamFrame(std::shared_ptr<Frame> frame);
    void HandleStreamDataBlockFrame(std::shared_ptr<Frame> frame);
    void HandleResetStreamFrame(std::shared_ptr<Frame> frame);

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
