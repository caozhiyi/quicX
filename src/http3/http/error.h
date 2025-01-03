#ifndef HTTP3_HTTP_CLIENT
#define HTTP3_HTTP_CLIENT

#include <cstdint>

namespace quicx {
namespace http3 {

enum HTTP3_ERROR_CODE: uint32_t {
    H3EC_NO_ERROR               = 0x0100, // No error. This is used when the connection or stream needs to be closed, but there is no error to signal.
    H3EC_GENERAL_PROTOCOL_ERROR = 0x0101, // Peer violated protocol requirements in a way that does not match a more specific error code or endpoint declines to use the more specific error code.
    H3EC_INTERNAL_ERROR         = 0x0102, // An internal error has occurred in the HTTP stack.
    H3EC_STREAM_CREATION_ERROR  = 0x0103, // The endpoint detected that its peer created a stream that it will not accept.
    H3EC_CLOSED_CRITICAL_STREAM = 0x0104, // A stream required by the HTTP/3 connection was closed or reset.
    H3EC_FRAME_UNEXPECTED       = 0x0105, // A frame was received that was not permitted in the current state or on the current stream.
    H3EC_FRAME_ERROR            = 0x0106, // A frame that fails to satisfy layout requirements or with an invalid size was received.
    H3EC_EXCESSIVE_LOAD         = 0x0107, // The endpoint detected that its peer is exhibiting a behavior that might be generating excessive load.
    H3EC_ID_ERROR               = 0x0108, // A stream ID or push ID was used incorrectly, such as exceeding a limit, reducing a limit, or being reused.
    H3EC_SETTINGS_ERROR         = 0x0109, // An endpoint detected an error in the payload of a SETTINGS frame.
    H3EC_MISSING_SETTINGS       = 0x010a, // No SETTINGS frame was received at the beginning of the control stream.
    H3EC_REQUEST_REJECTED       = 0x010b, // A server rejected a request without performing any application processing.
    H3EC_REQUEST_CANCELLED      = 0x010c, // The request or its response (including pushed response) is cancelled.
    H3EC_REQUEST_INCOMPLETE     = 0x010d, // The client's stream terminated without containing a fully formed request.
    H3EC_CONNECT_ERROR          = 0x010f, // The TCP connection established in response to a CONNECT request was reset or abnormally closed.
    H3EC_MESSAGE_ERROR          = 0x010e, // An HTTP message was malformed and cannot be processed.
    H3EC_VERSION_FALLBACK       = 0x0110, // The requested operation cannot be served over HTTP/3. The peer should retry over HTTP/1.1.
};

}
}

#endif