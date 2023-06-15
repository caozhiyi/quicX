#ifndef QUIC_INCLUDE_CONNECTION
#define QUIC_INCLUDE_CONNECTION

#include <string>

namespace quicx {

template <typename T = int>
class Connection<T> {
public:
    Connection() {}
    virtual ~Connection() {}

    virtual std::shared_ptr<ISendStream> MakeSendStream() = 0;
    virtual std::shared_ptr<BidirectionStream> MakeBidirectionalStream() = 0;

    virtual void Close(uint64_t error = 0) = 0;

    virtual void GetLocalAddr(std::string& addr, uint32_t& port);
    virtual void GetRemoteAddr(std::string& addr, uint32_t& port);

    void SetUserData(T user_data) { _user_data = user_data; }
    T GetUserData() { return _user_data; }
private:
    T _user_data;
};

}

#endif