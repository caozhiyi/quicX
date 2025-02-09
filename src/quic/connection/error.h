#ifndef QUIC_CONNECTION_ERROR
#define QUIC_CONNECTION_ERROR

#include <string>
#include <cstdint>

namespace quicx {
namespace quic {

enum QuicErrorCode: uint32_t {
    kNoError                 = 0x00, // an endpoint uses this with CONNECTION_CLOSE to signal that the connection is being closed abruptly in the absence of any error.
    kInternalError           = 0x01, // the endpoint encountered an internal error and cannot continue with the connection.
    kServerBusy              = 0x02, // the server is currently busy and does not accept any new connections.
    kFlowControlError        = 0x03, // received more data than it permitted in its advertised data limits.
    kStreamLimitError        = 0x04, // received a frame for a stream identifier that exceeded its advertised stream limit for the corresponding stream type.
    kStreamStateError        = 0x05, // received a frame for a stream that was not in a state that permitted that frame.
    kFinalSizeError          = 0x06, // received a STREAM frame containing data that exceeded the previously established final size.
    kFrameEncodingError      = 0x07, // received a frame that was badly formatted.
    kTransportParameterError = 0x08, // received transport parameters that were badly formatted, included an invalid value.
    kConnectionIdLimitError  = 0x09, // connection IDs provided by the peer exceeds the advertised active_connection_id_limit.
    kProtocolViolation       = 0x0a, // an error with protocol compliance that was not covered by more specific error codes.
    kInvalidToken            = 0x0b, // received a Retry Token in a client Initial that is invalid.
    kCryptoBufferExceeded    = 0x0d, // received more data in CRYPTO frames than it can buffer.
    kCryptoError             = 0x10, // cryptographic handshake failed.
    kConnectionTimeout       = 0x11, // connection timeout.
};

const std::string& GetErrorString(QuicErrorCode code);

}
}

#endif