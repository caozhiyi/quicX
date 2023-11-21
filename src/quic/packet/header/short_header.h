
#ifndef QUIC_PACKET_HEADER_SHORT_PACKET
#define QUIC_PACKET_HEADER_SHORT_PACKET

#include <memory>
#include "quic/common/constants.h"
#include "quic/packet/header/header_interface.h"

namespace quicx {
namespace quic {

class ShortHeader:
    public IHeader {
public:
    ShortHeader();
    ShortHeader(uint8_t flag);
    virtual ~ShortHeader();

    virtual bool EncodeHeader(std::shared_ptr<common::IBufferWrite> buffer);
    virtual bool DecodeHeader(std::shared_ptr<common::IBufferRead> buffer, bool with_flag = false);
    virtual uint32_t EncodeHeaderSize();

    void SetDestinationConnectionId(uint8_t* id, uint8_t len);
    uint8_t GetDestinationConnectionIdLength() { return _destination_connection_id_length; }
    const uint8_t* GetDestinationConnectionId()  { return _destination_connection_id; }
protected:
    uint32_t _destination_connection_id_length;
    uint8_t _destination_connection_id[__max_connection_length];
};

}
}

#endif