#include <ostream>
#include "common/network/address.h"

namespace quicx {
namespace common {

Address::Address():
    address_type_(AddressType::AT_IPV4),
    port_(0) {

}

Address::Address(AddressType at):
    address_type_(at),
    port_(0) {
    
}

Address::Address(const Address& addr):
    address_type_(addr.address_type_),
    ip_(addr.ip_),
    port_(addr.port_) {
}

Address::Address(const std::string& ip, uint16_t port):
    ip_(ip),
    port_(port) {
    address_type_ = CheckAddressType(ip);
}

Address::~Address() {

}

void Address::SetIp(const std::string& ip) {
    ip_ = ip;
}

const std::string& Address::GetIp() const {
    return ip_;
}

void Address::SetPort(uint16_t port) {
    port_ = port;
}

uint16_t Address::GetPort() const {
    return port_;
}

const std::string Address::AsString() const {
    return std::move(ip_ + ":" + std::to_string(port_));
}

std::ostream& operator<< (std::ostream &out, Address &addr) {
    const std::string str = addr.AsString();
    out.write(str.c_str(), str.length());
    return out;
}

bool operator==(const Address &addr1, const Address &addr2) {
    return addr1.ip_ == addr2.ip_ && addr1.port_ == addr2.port_ && addr1.port_ != 0;
}

AddressType Address::CheckAddressType(const std::string& ip) {
    if (ip.find(':') == std::string::npos) {
        return AddressType::AT_IPV4;
    }
    
    return AddressType::AT_IPV6;
}

}
}