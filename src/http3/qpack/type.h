#ifndef HTTP3_QPACK_TYPE
#define HTTP3_QPACK_TYPE

#include <string>

namespace quicx {
namespace http3 {

struct HeaderItem {
    std::string _name;
    std::string _value;
    HeaderItem(const std::string& name, const std::string& value) : _name(name), _value(value) {}
};


}
}

#endif
