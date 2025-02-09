#ifndef QUIC_CONNECTION_TYPE
#define QUIC_CONNECTION_TYPE

#include <cstdint>

namespace quicx {
namespace quic {

static const uint16_t kMaxCidLength =20;
static const uint16_t kMinCidLength = 4;

enum class SendOperation {
    kAllSendDone           = 0, // all data has been sent
    kSendAgainImmediately  = 1, // try send data again immediately
    kNextPeriod            = 2, // send data in next period
};

enum class TransportParamType : uint32_t {
    kOriginalDestinationConnectionId   = 0x00, // This parameter is the value of the Destination Connection ID field from the first Initial packet sent by the client;
    kMaxIdleTimeout                    = 0x01, // The maximum idle timeout is a value in milliseconds that is encoded as an integer
    kStatelessResetToken               = 0x02, // A stateless reset token is used in verifying a stateless reset
    kMaxUdpPayloadSize                 = 0x03, // The maximum UDP payload size parameter is an integer value that limits the size of UDP payloads that the endpoint is willing to receive
    kInitialMaxData                    = 0x04, // The initial maximum data parameter is an integer value that contains the initial value for
                                               // the maximum amount of data that can be sent on the connection.
    kInitialMaxStreamDataBidiLocal     = 0x05, // This parameter is an integer value specifying the initial flow control limit for locally initiated bidirectional streams
    kInitialMaxStreamDataBidiRemote    = 0x06, // This parameter is an integer value specifying the initial flow control limit for peer-initiated bidirectional streams.
    kInitialMaxStreamDataUni           = 0x07, // his parameter is an integer value specifying the initial 
                                               // flow control limit for unidirectional streams.
    kInitialMaxStreamsBidi             = 0x08, // The initial maximum bidirectional streams parameter is an
                                               // integer value that contains the initial maximum number of bidirectional streams the
                                               // endpoint that receives this transport parameter is permitted to initiate.
    kInitialMaxStreamsUni              = 0x09, // The initial maximum unidirectional streams parameter is an 
                                               // integer value that contains the initial maximum number of unidirectional streams the
                                               // endpoint that receives this transport parameter is permitted to initiate.
    kAckDelayExponent                  = 0x0a, // The acknowledgment delay exponent is an integer value indicating an exponent used to decode the ACK Delay field in the ACK frame
    kMaxAckDelay                       = 0x0b, // The maximum acknowledgment delay is an integer value indicating the
                                               // maximum amount of time in milliseconds by which the endpoint will delay sending acknowledgments.
    kDisableActiveMigration            = 0x0c, // The disable active migration transport parameter is included if
                                               // the endpoint does not support active connection migration (Section 9) on the address being used during the handshake
    kPreferredAddress                  = 0x0d, // The server's preferred address is used to effect a change in server address at the end of the handshake
    kActiveConnectionIdLimit           = 0x0e, // This is an integer value specifying the maximum number of connection IDs from the peer that an endpoint is willing to store
    kInitialSourceConnectionId         = 0x0f, // This is the value that the endpoint included in the Source Connection ID field of the first Initial packet it sends for the connection
    kRetrySourceConnectionId           = 0x10, // This is the value that the server included in the Source Connection ID field of a Retry packet
};

}
}
#endif