#ifndef HTTP3_CONNECTION_TYPE
#define HTTP3_CONNECTION_TYPE

#include <cstdint>

namespace quicx {
namespace http3 {

/**
 * @brief Settings type
 *
 * @note These definitions are based on the HTTP/3 RFC 9114, Section 7.2.4 "Settings Parameters"
 * @see https://datatracker.ietf.org/doc/html/rfc9114#section-7.2.4
 *
 * RFC 9114 §7.2.4.1: Setting identifiers 0x02, 0x03, 0x04, 0x05 are reserved
 * (HTTP/2 legacy) and MUST NOT be sent in a SETTINGS frame.
 * Receipt of these is treated as H3_SETTINGS_ERROR.
 */
enum SettingsType : uint16_t {
    kQpackMaxTableCapacity  = 0x01,
    // 0x02, 0x03, 0x04, 0x05 are RESERVED (RFC 9114 §7.2.4.1) — MUST NOT be sent on the wire
    kMaxFieldSectionSize    = 0x06,
    kQpackBlockedStreams    = 0x07,
    kEnableConnectProtocol  = 0x08,
};

}
}

#endif