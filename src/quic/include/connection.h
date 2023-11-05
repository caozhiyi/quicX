#ifndef QUIC_INCLUDE_CONNECTION
#define QUIC_INCLUDE_CONNECTION

#include <memory>
#include <string>

namespace quicx {

class ISendStream;
class BidirectionStream;
class Connection {
public:
    Connection() {}
    virtual ~Connection() {}

    virtual std::shared_ptr<ISendStream> MakeSendStream() = 0;
    virtual std::shared_ptr<BidirectionStream> MakeBidirectionalStream() = 0;

    virtual void Close(uint64_t error = 0) = 0;

    virtual void GetLocalAddr(std::string& addr, uint32_t& port) = 0;
    virtual void GetRemoteAddr(std::string& addr, uint32_t& port) = 0;

    void SetUserData(void* user_data) { _user_data = user_data; }
    void* GetUserData() { return _user_data; }
private:
    void* _user_data;
};

}

#endif