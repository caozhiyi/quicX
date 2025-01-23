#include <unordered_map>
#include "quic/connection/error.h"

namespace quicx {
namespace quic {

static const std::unordered_map<QUIC_ERROR_CODE, std::string> ErrorCodeReasons__ = {
    {QEC_NO_ERROR, "success"},
    {QEC_INTERNAL_ERROR, "internal error"},
    {QEC_SERVER_BUSY, "server busy"},
    {QEC_FLOW_CONTROL_ERROR, "flow control error"},
    {QEC_STREAM_LIMIT_ERROR, "stream limit error"},
    {QEC_STREAM_STATE_ERROR, "stream state error"},
    {QEC_FINAL_SIZE_ERROR, "final size error"},
    {QEC_FRAME_ENCODING_ERROR, "frame encoding error"},
    {QEC_TRANSPORT_PARAMETER_ERROR, "transport parameter error"},
    {QEC_CONNECTION_ID_LIMIT_ERROR, "connection id limit error"},
    {QEC_PROTOCOL_VIOLATION, "protocol violation"},
    {QEC_INVALID_TOKEN, "invalid token"},
    {QEC_CRYPTO_BUFFER_EXCEEDED, "crypto buffer exceeded"},
    {QEC_CRYPTO_ERROR, "crypto error"},
    {QEC_CONNECTION_TIMEOUT, "connection timeout"},
};

const std::string& GetErrorString(QUIC_ERROR_CODE code) {
    return ErrorCodeReasons__.at(code);
}

}
}