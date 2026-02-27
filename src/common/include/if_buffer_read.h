#ifndef COMMON_BUFFER_IF_BUFFER_READ
#define COMMON_BUFFER_IF_BUFFER_READ

#include <cstdint>
#include <functional>

namespace quicx {

/**
 * @brief Interface for reading data from buffers
 *
 * This interface provides read operations without modifying the underlying buffer state
 * unless explicitly requested through Read() or MoveReadPt().
 */
class IBufferRead {
public:
    IBufferRead() {}
    virtual ~IBufferRead() {}

    /**
     * @brief Read data without moving the read pointer
     *
     * @param data Destination buffer to copy data into
     * @param len Maximum number of bytes to read
     * @return Number of bytes actually read
     */
    virtual uint32_t ReadNotMovePt(uint8_t* data, uint32_t len) = 0;

    /**
     * @brief Move the read pointer forward
     *
     * @param len Number of bytes to advance
     * @return Number of bytes actually moved
     */
    virtual uint32_t MoveReadPt(uint32_t len) = 0;

    /**
     * @brief Read data and advance the read pointer
     *
     * @param data Destination buffer to copy data into
     * @param len Maximum number of bytes to read
     * @return Number of bytes actually read
     */
    virtual uint32_t Read(uint8_t* data, uint32_t len) = 0;

    /**
     * @brief Visit readable data segments without modifying state
     *
     * @param visitor Callback invoked for each readable segment
     */
    virtual void VisitData(const std::function<bool(uint8_t*, uint32_t)>& visitor) = 0;

    /**
     * @brief Get the length of readable data
     *
     * @return Number of bytes available to read
     */
    virtual uint32_t GetDataLength() = 0;

    /**
     * @brief Clear all data in the buffer
     */
    virtual void Clear() = 0;
};

}

#endif