#ifndef QUIC_CONNECTION_TRANSPORT_PARAM_TYPE
#define QUIC_CONNECTION_TRANSPORT_PARAM_TYPE

#include <cstdint>

namespace quicx {
namespace quic {

enum TransportParamType : uint32_t {
    TP_ORIGINAL_DESTINATION_CONNECTION_ID  = 0x00, // This parameter is the value of the Destination Connection ID field from the first Initial packet sent by the client;
    TP_MAX_IDLE_TIMEOUT                    = 0x01, // The maximum idle timeout is a value in milliseconds that is encoded as an integer
    TP_STATELESS_RESET_TOKEN               = 0x02, // A stateless reset token is used in verifying a stateless reset
    TP_MAX_UDP_PAYLOAD_SIZE                = 0x03, // The maximum UDP payload size parameter is an integer value that limits the size of UDP payloads that the endpoint is willing to receive
    TP_INITIAL_MAX_DATA                    = 0x04, // The initial maximum data parameter is an integer value that contains the initial value for
                                                   // the maximum amount of data that can be sent on the connection.
    TP_INITIAL_MAX_STREAM_DATA_BIDI_LOCAL  = 0x05, // This parameter is an integer value specifying the initial flow control limit for locally initiated bidirectional streams
    TP_INITIAL_MAX_STREAM_DATA_BIDI_REMOTE = 0x06, // This parameter is an integer value specifying the initial flow control limit for peer-initiated bidirectional streams.
    TP_INITIAL_MAX_STREAM_DATA_UNI         = 0x07, // his parameter is an integer value specifying the initial 
                                                   // flow control limit for unidirectional streams.
    TP_INITIAL_MAX_STREAMS_BIDI            = 0x08, // The initial maximum bidirectional streams parameter is an
                                                   // integer value that contains the initial maximum number of bidirectional streams the
                                                   // endpoint that receives this transport parameter is permitted to initiate.
    TP_INITIAL_MAX_STREAMS_UNI             = 0x09, // The initial maximum unidirectional streams parameter is an 
                                                   // integer value that contains the initial maximum number of unidirectional streams the
                                                   // endpoint that receives this transport parameter is permitted to initiate.
    TP_ACK_DELAY_EXPONENT                  = 0x0a, // The acknowledgment delay exponent is an integer value indicating an exponent used to decode the ACK Delay field in the ACK frame
    TP_MAX_ACK_DELAY                       = 0x0b, // The maximum acknowledgment delay is an integer value indicating the
                                                   // maximum amount of time in milliseconds by which the endpoint will delay sending acknowledgments.
    TP_DISABLE_ACTIVE_MIGRATION            = 0x0c, // The disable active migration transport parameter is included if
                                                   // the endpoint does not support active connection migration (Section 9) on the address being used during the handshake
    TP_PREFERRED_ADDRESS                   = 0x0d, // The server's preferred address is used to effect a change in server address at the end of the handshake
    TP_ACTIVE_CONNECTION_ID_LIMIT          = 0x0e, // This is an integer value specifying the maximum number of connection IDs from the peer that an endpoint is willing to store
    TP_INITIAL_SOURCE_CONNECTION_ID        = 0x0f, // This is the value that the endpoint included in the Source Connection ID field of the first Initial packet it sends for the connection
    TP_RETRY_SOURCE_CONNECTION_ID          = 0x10, // This is the value that the server included in the Source Connection ID field of a Retry packet
};

}
}

#endif