#ifndef HTTP3_QPACK_TYPE
#define HTTP3_QPACK_TYPE

#include <string>

namespace quicx {
namespace http3 {

struct HeaderItem {
    std::string _name;
    std::string _value;
};


}
}

#endif
