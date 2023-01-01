#ifndef QUIC_STREAM_RECV_STREAM
#define QUIC_STREAM_RECV_STREAM

#include <string>
#include "quic/stream/recv_stream_interface.h"

namespace quicx {

class RecvStream:
    public IRecvStream {
public:
    RecvStream(uint64_t id = 0);
    ~RecvStream();

    // abort reading
    void Close();

    void HandleFrame(std::shared_ptr<IFrame> frame);

private:
    void HandleStreamFrame(std::shared_ptr<IFrame> frame);
    void HandleStreamDataBlockFrame(std::shared_ptr<IFrame> frame);
    void HandleResetStreamFrame(std::shared_ptr<IFrame> frame);

private:
    uint32_t _data_limit;  // peer send data limit
    uint32_t _to_data_max; // if recv data more than this value, send max stream data frame
    uint64_t _final_offset;
};

}

#endif
