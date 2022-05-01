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

    virtual bool Encode(std::shared_ptr<IBufferWriteOnly> buffer);
    virtual bool Decode(std::shared_ptr<IBufferReadOnly> buffer, bool with_flag = false);
    virtual uint32_t EncodeSize();

    PacketType GetPacketType() const;
    uint32_t GetVersion() const;

protected:
    uint32_t _version;

    uint8_t _destination_connection_id_length;
    char _destination_connection_id[__max_connection_length];

    uint8_t _source_connection_id_length;
    char _source_connection_id[__max_connection_length];
};

}

#endif