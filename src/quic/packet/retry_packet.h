
#ifndef QUIC_PACKET_RETRY_PACKET
#define QUIC_PACKET_RETRY_PACKET

#include "quic/packet/long_header.h"

namespace quicx {

class RetryPacket: public LongHeader {
public:
    RetryPacket();
    virtual ~RetryPacket();

    virtual bool Encode(std::shared_ptr<Buffer> buffer, std::shared_ptr<AlloterWrap> alloter);
    virtual bool Decode(std::shared_ptr<Buffer> buffer, std::shared_ptr<AlloterWrap> alloter);
    virtual uint32_t EncodeSize();

    virtual bool AddFrame(std::shared_ptr<Frame> frame);

private:
    char* _retry_token;
    char _retry_integrity_tag[128];
};

}

#endif