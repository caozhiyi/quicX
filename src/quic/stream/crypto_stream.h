#ifndef QUIC_STREAM_CRYPTO_STREAM
#define QUIC_STREAM_CRYPTO_STREAM

#include "quic/stream/bidirection_stream.h"

namespace quicx {

class CryptoStream:
    public BidirectionStream {
public:
    CryptoStream(std::shared_ptr<BlockMemoryPool> alloter, uint64_t id = 0);
    virtual ~CryptoStream();

    virtual bool TrySendData(IDataVisitor* visitior);
};

}

#endif