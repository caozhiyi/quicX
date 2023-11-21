#ifndef QUIC_INCLUDE_SEND_STREAM
#define QUIC_INCLUDE_SEND_STREAM

#include <string>
#include <cstdint>

namespace quicx {
namespace quic {

class SendStream {
public:
    SendStream() {}
    virtual ~SendStream() {}

    virtual int32_t Send(const std::string& data);
    virtual int32_t Send(u_char* data, uint32_t len);

    virtual void Close() = 0;

    virtual void Reset(uint64_t error = 0) = 0;

    virtual uint64_t GetStreamID() = 0;

    void SetUserData(void* user_data) { _user_data = user_data; }
    void* GetUserData() { return _user_data; }
private:
    void* _user_data;
};

}
}

#endif