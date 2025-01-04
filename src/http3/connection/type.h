#ifndef HTTP3_CONNECTION_TYPE
#define HTTP3_CONNECTION_TYPE



namespace quicx {
namespace http3 {
// These definitions are based on the HTTP/3 RFC 9114, Section 7.2.4 "Settings Parameters"
// Reference: https://datatracker.ietf.org/doc/html/rfc9114#section-7.2.4
enum SETTINGS_TYPE {
    ST_QPACK_MAX_TABLE_CAPACITY = 0x01,
    ST_ENABLE_PUSH              = 0x02,
    ST_MAX_CONCURRENT_STREAMS   = 0x03,
    ST_MAX_FRAME_SIZE           = 0x04,
    ST_MAX_HEADER_LIST_SIZE     = 0x05,
    ST_MAX_FIELD_SECTION_SIZE   = 0x06,
    ST_QPACK_BLOCKED_STREAMS    = 0x07,
    ST_ENABLE_CONNECT_PROTOCOL  = 0x08,
};

}
}

#endif