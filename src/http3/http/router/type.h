#ifndef HTTP3_HTTP_ROUTER_TYPE
#define HTTP3_HTTP_ROUTER_TYPE

namespace quicx {
namespace http3 {

enum RouterErrorCode {
    REC_SUCCESS       = 0,
    REC_UNFIND        = 1, // can't find the resource
    REC_INVAILID_PATH = 2, // invalid path
};

}
}

#endif
