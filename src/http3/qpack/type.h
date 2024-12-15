#ifndef HTTP3_QPACK_TYPE
#define HTTP3_QPACK_TYPE

#include <string>

namespace quicx {
namespace http3 {

struct HeaderItem {
    std::string name_;
    std::string value_;
    HeaderItem(const std::string& name, const std::string& value) : name_(name), value_(value) {}
};


}
}

#endif
