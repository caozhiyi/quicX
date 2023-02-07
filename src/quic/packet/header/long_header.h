#ifndef QUIC_PACKET_HEADER_LONG_PACKET
#define QUIC_PACKET_HEADER_LONG_PACKET

#include <memory>
#include "quic/packet/type.h"
#include "quic/common/constants.h"
#include "quic/packet/header/header_interface.h"

namespace quicx {

class LongHeader:
    public IHeader {
public:
    LongHeader();
    LongHeader(uint8_t flag);
    virtual ~LongHeader();

    virtual bool EncodeHeader(std::shared_ptr<IBufferWrite> buffer);
    virtual bool DecodeHeader(std::shared_ptr<IBufferRead> buffer, bool with_flag = false);
    virtual uint32_t EncodeHeaderSize();

    uint32_t GetVersion() const;
    uint8_t GetDestinationConnectionIdLength() { return _destination_connection_id_length; }
    const uint8_t* GetDestinationConnectionId()  { return _destination_connection_id; }
    uint8_t GetSourceConnectionIdLength() { return _source_connection_id_length; }
    const uint8_t* GetSourceConnectionId()  { return _source_connection_id; }

protected:
    uint32_t _version;

    uint8_t _destination_connection_id_length;
    uint8_t _destination_connection_id[__max_connection_length];

    uint8_t _source_connection_id_length;
    uint8_t _source_connection_id[__max_connection_length];
};

}

#endif