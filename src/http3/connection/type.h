#ifndef HTTP3_CONNECTION_TYPE
#define HTTP3_CONNECTION_TYPE

namespace quicx {
namespace http3 {

// These definitions are based on the HTTP/3 RFC 9114, Section 7.2.4 "Settings Parameters"
// Reference: https://datatracker.ietf.org/doc/html/rfc9114#section-7.2.4
enum SettingsType : uint16_t {
    kQpackMaxTableCapacity  = 0x01,
    kEnablePush             = 0x02,
    kMaxConcurrentStreams   = 0x03,
    kMaxFrameSize           = 0x04,
    kMaxHeaderListSize      = 0x05,
    kMaxFieldSectionSize    = 0x06,
    kQpackBlockedStreams    = 0x07,
    kEnableConnectProtocol  = 0x08,
};

}
}

#endif