#ifndef QUIC_COMMON_NETWORK_ADDRESS
#define QUIC_COMMON_NETWORK_ADDRESS

#include <string>
#include <cstdint>

namespace quicx {
namespace common {

enum class AddressType {
    kIpv4  = 0x1,
    kIpv6  = 0x2,
};

class Address {
public:
    Address();
    Address(AddressType at);
    Address(const Address& addr);
    Address(const std::string& ip, uint16_t port);
    ~Address();

    virtual void SetIp(const std::string& ip);
    virtual const std::string& GetIp() const;

    virtual void SetPort(uint16_t port);
    virtual uint16_t GetPort() const;

    virtual void SetAddressType(AddressType address_type) { address_type_ = address_type; }
    virtual AddressType GetAddressType() const { return address_type_; }

    virtual const std::string AsString() const;

    friend std::ostream& operator<< (std::ostream &out, Address &addr);
    friend bool operator==(const Address &addr1, const Address &addr2);

    static AddressType CheckAddressType(const std::string& ip);

protected:
    AddressType address_type_;
    std::string ip_;
    uint16_t port_;
};

}
}

#endif