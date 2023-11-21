#ifndef QUIC_STREAM_TYPE
#define QUIC_STREAM_TYPE

namespace quicx {
namespace quic {

enum StreamType {
    ST_CLIENT_BIDIRECTIONAL  = 0x00,
    ST_SERVER_BIDIRECTIONAL  = 0x01,
    ST_CLIENT_UNIDIRECTIONAL = 0x02,
    ST_SERVER_UNIDIRECTIONAL = 0x03
};

enum StreamState {
    SS_UNKNOW      = 0,
    // sending stream states
    SS_READY       = 0x0001,
    SS_SEND        = 0x0002,
    SS_DATA_SENT   = 0x0004,
    SS_RESET_SENT  = 0x0008,
    
    // receiving stream states
    SS_RECV        = 0x0010,
    SS_SIZE_KNOWN  = 0x0020,
    SS_DATA_READ   = 0x0040,
    SS_RESET_READ  = 0x0080,

    // common termination states
    SS_DATA_RECVD  = 0x0100,
    SS_RESET_RECVD = 0x0200,
};

}
}

#endif