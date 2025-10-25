#ifndef HTTP3_HTTP_ERROR
#define HTTP3_HTTP_ERROR

#include <cstdint>

namespace quicx {
namespace http3 {

enum Http3ErrorCode: uint32_t {
    kNoError                    = 0x0100, // No error. This is used when the connection or stream needs to be closed, but there is no error to signal.
    kGeneralProtocolError       = 0x0101, // Peer violated protocol requirements in a way that does not match a more specific error code or endpoint declines to use the more specific error code.
    kInternalError              = 0x0102, // An internal error has occurred in the HTTP stack.
    kStreamCreationError        = 0x0103, // The endpoint detected that its peer created a stream that it will not accept.
    kClosedCriticalStream       = 0x0104, // A stream required by the HTTP/3 connection was closed or reset.
    kFrameUnexpected            = 0x0105, // A frame was received that was not permitted in the current state or on the current stream.
    kFrameError                 = 0x0106, // A frame that fails to satisfy layout requirements or with an invalid size was received.
    kExcessiveLoad              = 0x0107, // The endpoint detected that its peer is exhibiting a behavior that might be generating excessive load.
    kIdError                    = 0x0108, // A stream ID or push ID was used incorrectly, such as exceeding a limit, reducing a limit, or being reused.
    kSettingsError              = 0x0109, // An endpoint detected an error in the payload of a SETTINGS frame.
    kMissingSettings            = 0x010a, // No SETTINGS frame was received at the beginning of the control stream.
    kRequestRejected            = 0x010b, // A server rejected a request without performing any application processing.
    kRequestCancelled           = 0x010c, // The request or its response (including pushed response) is cancelled.
    kRequestIncomplete          = 0x010d, // The client's stream terminated without containing a fully formed request.
    kConnectError               = 0x010f, // The TCP connection established in response to a CONNECT request was reset or abnormally closed.
    kMessageError               = 0x010e, // An HTTP message was malformed and cannot be processed.
    kVersionFallback            = 0x0110, // The requested operation cannot be served over HTTP/3. The peer should retry over HTTP/1.1.
    
    // QPACK Error Codes (RFC 9204 Section 3.2)
    kQpackDecompressionFailed   = 0x0200, // Decompression of a header block failed.
    kQpackEncoderStreamError    = 0x0201, // Error on the encoder stream (e.g., malformed encoder instruction).
    kQpackDecoderStreamError    = 0x0202, // Error on the decoder stream (e.g., malformed decoder instruction).
};

}
}

#endif