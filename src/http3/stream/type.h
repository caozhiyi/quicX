#ifndef HTTP3_STREAM_TYPE
#define HTTP3_STREAM_TYPE

namespace quicx {
namespace http3 {

enum StreamType {
    ST_CONTROL = 0x00,
    ST_REQUEST = 0x01,
    ST_PUSH    = 0x02,
};

}
}

#endif
