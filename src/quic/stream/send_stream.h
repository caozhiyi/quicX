#ifndef QUIC_STREAM_SEND_STREAM
#define QUIC_STREAM_SEND_STREAM

#include <string>
#include "quic/stream/send_stream_interface.h"

namespace quicx {

class SendStream:
    public ISendStream {
public:
    SendStream(uint64_t id = 0);
    virtual ~SendStream();

    virtual int32_t Send(uint8_t* data, uint32_t len);

    // reset the stream
    virtual void Reset(uint64_t err);
 
    // end the stream
    virtual void Close();

    virtual void HandleFrame(std::shared_ptr<IFrame> frame);

private:
    void HandleMaxStreamDataFrame(std::shared_ptr<IFrame> frame);
    void HandleStopSendingFrame(std::shared_ptr<IFrame> frame);

private:
    uint64_t _data_offset;
    uint64_t _peer_data_limit;
};

}

#endif
