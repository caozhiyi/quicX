#ifndef HTTP3_STREAM_TYPE
#define HTTP3_STREAM_TYPE

namespace quicx {
namespace http3 {

enum class StreamType {
    kControl      = 0x00,   // control stream (RFC 9114)
    kPush         = 0x01,   // push stream (RFC 9114 Section 4.6)
    kQpackEncoder = 0x02,   // QPACK encoder stream (RFC 9114)
    kQpackDecoder = 0x03,   // QPACK decoder stream (RFC 9114)
    // Note: Request-Response streams use bidirectional QUIC streams and don't have a unidirectional stream type
    kReqResp      = 0xFF,   // Internal use only for request/response streams (not sent on wire)
    kUnidentified = 0xFFFF, // Unidentified stream (not sent on wire)
};

}
}

#endif
