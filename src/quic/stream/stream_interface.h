#ifndef QUIC_STREAM_TYPE
#define QUIC_STREAM_TYPE

#include <string>
#include <cstdint>

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
};

}

#endif
