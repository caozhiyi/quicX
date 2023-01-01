#ifndef QUIC_PACKET_LONG_PACKET
#define QUIC_PACKET_LONG_PACKET

#include <memory>
#include "quic/packet/type.h"
#include "quic/common/constants.h"
#include "quic/packet/header_interface.h"

namespace quicx {

class LongHeader:
    public IHeader {
public:
    LongHeader();
    LongHeader(HeaderFlag flag);
    virtual ~LongHeader();

    virtual bool Encode(std::shared_ptr<IBufferWrite> buffer);
    virtual bool Decode(std::shared_ptr<IBufferRead> buffer, bool with_flag = false);
    virtual uint32_t EncodeSize();

    PacketType GetPacketType() const;
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