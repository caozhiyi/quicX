#include <unordered_map>
#include "quic/connection/error.h"

namespace quicx {
namespace quic {

static const std::unordered_map<QuicErrorCode, std::string> kErrorCodeReasons = {
    {kNoError, "success"},
    {kInternalError, "internal error"},
    {kServerBusy, "server busy"},
    {kFlowControlError, "flow control error"},
    {kStreamLimitError, "stream limit error"},
    {kStreamStateError, "stream state error"},
    {kFinalSizeError, "final size error"},
    {kFrameEncodingError, "frame encoding error"},
    {kTransportParameterError, "transport parameter error"},
    {kConnectionIdLimitError, "connection id limit error"},
    {kProtocolViolation, "protocol violation"},
    {kInvalidToken, "invalid token"},
    {kCryptoBufferExceeded, "crypto buffer exceeded"},
    {kCryptoError, "crypto error"},
    {kConnectionTimeout, "connection timeout"},
};

const std::string& GetErrorString(QuicErrorCode code) {
    return kErrorCodeReasons.at(code);
}

}
}