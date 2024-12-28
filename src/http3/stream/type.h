#ifndef HTTP3_STREAM_TYPE
#define HTTP3_STREAM_TYPE

namespace quicx {
namespace http3 {

enum StreamType {
    ST_CONTROL  = 0x00,
    ST_REQ_RESP = 0x01,
    ST_PUSH     = 0x02,
};

// defined in RFC 9214
// https://httpwg.org/specs/rfc9114.html#frame-settings
enum SettingsType: uint16_t {
    ST_QPACK_MAX_TABLE_CAPACITY = 0x01,
    ST_MAX_HEADER_LIST_SIZE     = 0x06,
    ST_QPACK_BLOCKED_STREAMS    = 0x07,
    ST_ENABLE_CONNECT_PROTOCOL  = 0x08,
};

}
}

#endif
