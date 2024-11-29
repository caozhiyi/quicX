#ifndef QUIC_PACKET_HEADER_LONG_PACKET
#define QUIC_PACKET_HEADER_LONG_PACKET

#include <memory>
#include "quic/packet/type.h"
#include "quic/common/constants.h"
#include "quic/packet/header/if_header.h"

namespace quicx {
namespace quic {

class LongHeader:
    public IHeader {
public:
    LongHeader();
    LongHeader(uint8_t flag);
    virtual ~LongHeader();

    virtual bool EncodeHeader(std::shared_ptr<common::IBufferWrite> buffer);
    virtual bool DecodeHeader(std::shared_ptr<common::IBufferRead> buffer, bool with_flag = false);
    virtual uint32_t EncodeHeaderSize();

    void SetVersion(uint32_t version) { version_ = version; } 
    uint32_t GetVersion() const {  return version_; }

    void SetDestinationConnectionId(uint8_t* id, uint8_t len);
    uint8_t GetDestinationConnectionIdLength() { return destination_connection_id_length_; }
    const uint8_t* GetDestinationConnectionId()  { return destination_connection_id_; }

    void SetSourceConnectionId(uint8_t* id, uint8_t len);
    uint8_t GetSourceConnectionIdLength() { return source_connection_id_length_; }
    const uint8_t* GetSourceConnectionId()  { return source_connection_id_; }

protected:
    uint32_t version_;

    uint8_t destination_connection_id_length_;
    uint8_t destination_connection_id_[__max_connection_length];

    uint8_t source_connection_id_length_;
    uint8_t source_connection_id_[__max_connection_length];
};

}
}

#endif