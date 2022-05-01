
#ifndef QUIC_PACKET_RETRY_PACKET
#define QUIC_PACKET_RETRY_PACKET

#include <memory>
#include "quic/packet/packet_interface.h"
#include "quic/packet/header_interface.h"

namespace quicx {

class RetryPacket:
    public IPacket {
public:
    RetryPacket(std::shared_ptr<IHeader> header);
    virtual ~RetryPacket();

    virtual bool Encode(std::shared_ptr<IBufferWriteOnly> buffer);
    virtual bool Decode(std::shared_ptr<IBufferReadOnly> buffer);
    virtual uint32_t EncodeSize();

    virtual bool AddFrame(std::shared_ptr<IFrame> frame);

private:
    char* _retry_token;
    char _retry_integrity_tag[128];
};

}

#endif