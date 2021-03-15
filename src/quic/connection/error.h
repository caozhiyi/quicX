#ifndef QUIC_TRANSPORT_ERROR
#define QUIC_TRANSPORT_ERROR

namespace quicx {

enum QUIC_ERROR_CODE {
    QEC_NO_ERROR                  = 0x00, // an endpoint uses this with CONNECTION_CLOSE to signal that the connection is being closed abruptly in the absence of any error.
    QEC_INTERNAL_ERROR            = 0x01, // the endpoint encountered an internal error and cannot continue with the connection.
    QEC_SERVER_BUSY               = 0x02, // the server is currently busy and does not accept any new connections.
    QEC_FLOW_CONTROL_ERROR        = 0x03, // received more data than it permitted in its advertised data limits.
    QEC_STREAM_LIMIT_ERROR        = 0x04, // received a frame for a stream identifier that exceeded its advertised stream limit for the corresponding stream type.
    QEC_STREAM_STATE_ERROR        = 0x05, // received a frame for a stream that was not in a state that permitted that frame.
    QEC_FINAL_SIZE_ERROR          = 0x06, // received a STREAM frame containing data that exceeded the previously established final size.
    QEC_FRAME_ENCODING_ERROR      = 0x07, // received a frame that was badly formatted.
    QEC_TRANSPORT_PARAMETER_ERROR = 0x08, // received transport parameters that were badly formatted, included an invalid value.
    QEC_CONNECTION_ID_LIMIT_ERROR = 0x09, // connection IDs provided by the peer exceeds the advertised active_connection_id_limit.
    QEC_PROTOCOL_VIOLATION        = 0x0a, // an error with protocol compliance that was not covered by more specific error codes.
    QEC_INVALID_TOKEN             = 0x0b, // received a Retry Token in a client Initial that is invalid.
    QEC_CRYPTO_BUFFER_EXCEEDED    = 0x0d, // received more data in CRYPTO frames than it can buffer.
    QEC_CRYPTO_ERROR              = 0x10, // cryptographic handshake failed.
};

}

#endif