#ifndef QUIC_INCLUDE_IF_QUIC_CONNECTION
#define QUIC_INCLUDE_IF_QUIC_CONNECTION

#include <string>
#include "quic/include/type.h"

namespace quicx {

/**
 * @brief Represents a live QUIC connection.
 *
 * The interface allows applications to bind metadata, create streams and close
 * the transport in a controlled fashion. Implementations guarantee thread-safe
 * hand-off to the worker thread that owns the connection.
 */
class IQuicConnection {
public:
    IQuicConnection() {}
    virtual ~IQuicConnection() {}

    /**
     * @brief Attach opaque user data to the connection.
     *
     * Ownership remains with the caller; the connection simply stores the raw
     * pointer for later retrieval.
     */
    virtual void SetUserData(void* user_data) = 0;
    virtual void* GetUserData() = 0;

    /**
     * @brief Query the peer's address as observed by the transport.
     *
     * @param addr Filled with the string representation of the remote IP.
     * @param port Filled with the remote UDP port.
     */
    virtual void GetRemoteAddr(std::string& addr, uint32_t& port) = 0;

    /**
     * @brief Close the connection gracefully.
     *
     * Oustanding streams get a chance to flush their send buffers; FIN frames
     * are exchanged before the connection shuts down.
     */
    virtual void Close() = 0;

    /**
     * @brief Abort the connection immediately and notify the peer with an error.
     *
     * @param error_code Application-defined reason sent to the remote side.
     */
    virtual void Reset(uint32_t error_code) = 0;

    /**
     * @brief Create a new application stream.
     *
     * Only unidirectional-sender and bidirectional streams can be opened
     * locally. Receive-only streams are delivered via callbacks.
     *
     * @param type Desired stream direction.
     */
    virtual std::shared_ptr<IQuicStream> MakeStream(StreamDirection type) = 0;

    /**
     * @brief Install a callback that reports stream lifecycle changes.
     *
     * @param cb Callback invoked when streams are created, closed or error out.
     */
    virtual void SetStreamStateCallBack(stream_state_callback cb) = 0;
};

}


#endif