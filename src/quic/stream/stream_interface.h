#ifndef QUIC_STREAM_STREAM_INTERFACE
#define QUIC_STREAM_STREAM_INTERFACE

#include <memory>
#include <cstdint>
#include <functional>

#include "type.h"

namespace quicx {

class IFrame;
class Buffer;
class Connection;
class StreamStateMachine;

typedef std::function<void(std::shared_ptr<Buffer>, int32_t/*errno*/)> StreamReadBack;
typedef std::function<void(uint32_t, int32_t/*errno*/)> StreamWriteBack;

class Stream {
public:
    Stream(StreamType type): _stream_type(type), _stream_id(0) {}
    virtual ~Stream() {}

    virtual void Close() = 0;

    virtual void HandleFrame(std::shared_ptr<IFrame> frame) = 0;

protected:
    void SetStreamID(uint64_t id) { _stream_id = id; }
    uint64_t GetStreamID() { return _stream_id; }

    void SetConnection(std::shared_ptr<Connection> connection) { _connection = connection; }
    std::shared_ptr<Connection> GetConnection() { return _connection; }

protected:
    StreamType _stream_type;
    uint64_t _stream_id;
    std::shared_ptr<Connection> _connection;
    std::shared_ptr<StreamStateMachine> _state_machine;
};

}

#endif
