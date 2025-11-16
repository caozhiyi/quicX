
#ifndef QUIC_PACKET_HEADER_SHORT_PACKET
#define QUIC_PACKET_HEADER_SHORT_PACKET

#include <memory>
#include "quic/common/constants.h"
#include "quic/packet/header/if_header.h"

namespace quicx {
namespace quic {

class ShortHeader:
    public IHeader {
public:
    ShortHeader();
    ShortHeader(uint8_t flag);
    virtual ~ShortHeader();

    virtual bool EncodeHeader(std::shared_ptr<common::IBuffer> buffer);
    virtual bool DecodeHeader(std::shared_ptr<common::IBuffer> buffer, bool with_flag = false);
    virtual uint32_t EncodeHeaderSize();

    void SetDestinationConnectionId(const uint8_t* id, uint8_t len);
    uint8_t GetDestinationConnectionIdLength() { return destination_connection_id_length_; }
    const uint8_t* GetDestinationConnectionId()  { return destination_connection_id_; }
protected:
    uint32_t destination_connection_id_length_;
    uint8_t destination_connection_id_[kMaxConnectionLength];
};

}
}

#endif