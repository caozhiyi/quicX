#ifndef QUIC_STREAM_INTERFACE
#define QUIC_STREAM_INTERFACE

#include <string>
#include <cstdint>
#include "type.h"

namespace qucix {

class Stream {
public:
    Stream();
    ~Stream();

    int32_t Write(const std::string &data);
    int32_t Write(std::string &data);
    int32_t Write(char* data, uint32_t len);

    void Close();

    void Reset();

    void SetReadCallBack();
private:
    StreamType _stream_type;
    uint64_t _data_offset;
    uint64_t _peer_data_limit;
};

}

#endif
