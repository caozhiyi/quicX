#ifndef HTTP3_STREAM_TYPE
#define HTTP3_STREAM_TYPE

namespace quicx {
namespace http3 {

enum class StreamType {
    kControl    = 0x00, // control stream
    kReqResp    = 0x01, // request response stream
    kPush       = 0x02, // push stream
};

}
}

#endif
