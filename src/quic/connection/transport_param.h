#ifndef QUIC_CONNECTION_TRANSPORT_PARAM
#define QUIC_CONNECTION_TRANSPORT_PARAM

namespace quicx {

enum TransportParam {
    original_destination_connection_id  = 0x00, // This parameter is the value of the Destination Connection ID field from the first Initial packet sent by the client;
    max_idle_timeout                    = 0x01, // The maximum idle timeout is a value in milliseconds that is encoded as an integer
    stateless_reset_token               = 0x02, // A stateless reset token is used in verifying a stateless reset
    max_udp_payload_size                = 0x03, // The maximum UDP payload size parameter is an integer value that limits the size of UDP payloads that the endpoint is willing to receive
    initial_max_data                    = 0x04, // The initial maximum data parameter is an integer value that contains the initial value for
                                                // the maximum amount of data that can be sent on the connection.
    initial_max_stream_data_bidi_local  = 0x05, // This parameter is an integer value specifying the initial flow control limit for locally initiated bidirectional streams
    initial_max_stream_data_bidi_remote = 0x06, // This parameter is an integer value specifying the initial flow control limit for peer-initiated bidirectional streams.
    initial_max_stream_data_uni         = 0x07, // his parameter is an integer value specifying the initial 
                                                // flow control limit for unidirectional streams.
    initial_max_streams_bidi            = 0x08, // The initial maximum bidirectional streams parameter is an
                                                // integer value that contains the initial maximum number of bidirectional streams the
                                                // endpoint that receives this transport parameter is permitted to initiate.
    initial_max_streams_uni             = 0x09, // The initial maximum unidirectional streams parameter is an 
                                                // integer value that contains the initial maximum number of unidirectional streams the
                                                // endpoint that receives this transport parameter is permitted to initiate.
    ack_delay_exponent                  = 0x0a, // The acknowledgment delay exponent is an integer value indicating an exponent used to decode the ACK Delay field in the ACK frame
    max_ack_delay                       = 0x0b, // The maximum acknowledgment delay is an integer value indicating the
                                                // maximum amount of time in milliseconds by which the endpoint will delay sending acknowledgments.
    disable_active_migration            = 0x0c, // The disable active migration transport parameter is included if
                                                // the endpoint does not support active connection migration (Section 9) on the address being used during the handshake
    preferred_address                   = 0x0d, // The server's preferred address is used to effect a change in server address at the end of the handshake
    active_connection_id_limit          = 0x0e, // This is an integer value specifying the maximum number of connection IDs from the peer that an endpoint is willing to store
    initial_source_connection_id        = 0x0f, // This is the value that the endpoint included in the Source Connection ID field of the first Initial packet it sends for the connection
    retry_source_connection_id          = 0x10, // This is the value that the server included in the Source Connection ID field of a Retry packet
};

}

#endif