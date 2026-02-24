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

    /**
     * @brief Check if the buffer chunk is valid
     *
     * @return true if the buffer chunk contains valid memory
     * @return false if the buffer chunk is null or invalid
     */
    virtual bool Valid() const = 0;

    /**
     * @brief Get the raw data pointer of the buffer
     *
     * @return uint8_t* pointer to the start of the buffer data
     */
    virtual uint8_t* GetData() const = 0;

    /**
     * @brief Set the current valid size/limit of the buffer
     *
     * @param size size of valid data in bytes
     */
    virtual void SetLimitSize(uint32_t size) = 0;

    /**
     * @brief Get the current valid length of the buffer
     *
     * @return uint32_t size of valid data in bytes
     */
    virtual uint32_t GetLength() const = 0;

    /**
     * @brief Get the memory pool this buffer chunk belongs to
     *
     * @return std::shared_ptr<BlockMemoryPool> shared pointer to the memory pool
     */
    virtual std::shared_ptr<BlockMemoryPool> GetPool() const = 0;
};

}  // namespace common
}  // namespace quicx

#endif
