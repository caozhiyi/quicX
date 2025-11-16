#ifndef COMMON_BUFFER_IF_BUFFER_CHUNK
#define COMMON_BUFFER_IF_BUFFER_CHUNK

#include <cstdint>
#include <memory>

namespace quicx {
namespace common {

class BlockMemoryPool;

class IBufferChunk {
public:
    virtual ~IBufferChunk() = default;

    virtual bool Valid() const = 0;
    virtual uint8_t* GetData() const = 0;
    virtual uint32_t GetLength() const = 0;
    virtual std::shared_ptr<BlockMemoryPool> GetPool() const = 0;
};

}
}

#endif

