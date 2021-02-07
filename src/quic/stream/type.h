#ifndef QUIC_STREAM_TYPE
#define QUIC_STREAM_TYPE

namespace qucix {

enum StreamType {
    ST_CLIENT_BIDIRECTIONAL = 0x00,
    ST_SERVER_BIDIRECTIONAL = 0x01,
    ST_CLIENT_BIDIRECTIONAL = 0x02,
    ST_SERVER_UNIDIRCTIONAL = 0x03
};

enum StreamStates {
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

/* * * * * * * * * * * * * * * * * * * * * 

sending stream states
    o    
    | Create Stream (Sending)    
    | Peer Creates Bidirectional Stream    
    v    
+-------+    
| Ready | Send RESET_STREAM    
|       |-----------------------.    
+-------+                       |    
    |                           |       
    | Send STREAM /             |    
    | STREAM_DATA_BLOCKED       |    
    |                           |    
    | Peer Creates              |    
    | Bidirectional Stream      |    
    v                           |    
+-------+                       |    
| Send  | Send RESET_STREAM     |    
|       |---------------------->|    
+-------+                       |    
    |                           |    
    | Send STREAM + FIN         |    
    v                           v    
+-------+                   +-------+
| Data  | Send RESET_STREAM | Reset |
| Sent  |------------------>| Sent  |
+-------+                   +-------+
    |                           |
    | Recv All ACKs             | Recv ACK
    v                           v
+-------+                   +-------+
| Data  |                   | Reset |
| Recvd |                   | Recvd |
+-------+                   +-------+

receiving stream states
    o
    | Recv STREAM / STREAM_DATA_BLOCKED / RESET_STREAM
    | Create Bidirectional Stream (Sending)
    | Recv MAX_STREAM_DATA / STOP_SENDING (Bidirectional)
    | Create Higher-Numbered Stream
    v
+-------+
|  Recv | Recv RESET_STREAM
|       |-----------------------.
+-------+                       |
    |                           |  
    | Recv STREAM + FIN         |
    v                           |
+-------+                       |
| Size  |   Recv RESET_STREAM   |
| Known |---------------------->|
+-------+                       |
    |                           |
    |Recv All Data              |
    v                           v
+-------+ Recv RESET_STREAM +-------+
| Data  |--- (optional) --->| Reset |
| Recvd |   Recv All Data   | Recvd |
+-------+<-- (optional) ----+-------+
    |                           |
    | App Read All Data         | App Read RST
    v                           v
+-------+                   +-------+
| Data  |                   | Reset |
| Read  |                   | Read  |
+-------+                   +-------+

* * * * * * * * * * * * * * * * * * * * */

}

#endif