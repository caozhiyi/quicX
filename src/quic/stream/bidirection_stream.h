#ifndef QUIC_STREAM_BIDIRECTION_STREAM
#define QUIC_STREAM_BIDIRECTION_STREAM

#include "stream_interface.h"

namespace quicx {

class RecvStream;
class SendStream;
class BidirectionStream: public Stream {
public:
    BidirectionStream(StreamType type);
    ~BidirectionStream();

    int32_t Write(std::shared_ptr<Buffer> buffer, uint32_t len = 0);
    int32_t Write(const std::string &data);
    int32_t Write(char* data, uint32_t len);

    void Close();
    void Reset(uint64_t err);

    void SetReadCallBack(StreamReadBack rb);
    void SetWriteCallBack(StreamWriteBack wb);

    void SetDataLimit(uint32_t limit);
    uint32_t GetDataLimit();

    void SetToDataMax(uint32_t to_data_max);
    uint32_t GetToDataMax();

    void HandleFrame(std::shared_ptr<Frame> frame);

protected:
    std::shared_ptr<RecvStream> _recv_stream;
    std::shared_ptr<SendStream> _send_stream;
};

}

#endif