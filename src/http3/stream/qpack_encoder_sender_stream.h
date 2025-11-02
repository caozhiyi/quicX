#ifndef HTTP3_STREAM_QPACK_ENCODER_SENDER_STREAM
#define HTTP3_STREAM_QPACK_ENCODER_SENDER_STREAM

#include <vector>
#include <memory>
#include "http3/stream/if_send_stream.h"
#include "quic/include/if_quic_send_stream.h"

namespace quicx {
namespace http3 {

class QpackEncoderSenderStream:
    public ISendStream {
public:
    explicit QpackEncoderSenderStream(const std::shared_ptr<quic::IQuicSendStream>& stream,
        const std::function<void(uint64_t stream_id, uint32_t error_code)>& error_handler);
    virtual ~QpackEncoderSenderStream();

    // Write QPACK encoder stream type (0x02) and send instruction bytes
    bool SendInstructions(const std::vector<uint8_t>& blob);

    // High-level helpers to send explicit encoder instructions
    bool SendSetCapacity(uint64_t capacity);
    bool SendInsertWithNameRef(bool is_static, uint64_t name_index, const std::string& value);
    bool SendInsertWithoutNameRef(const std::string& name, const std::string& value);
    bool SendDuplicate(uint64_t index);
};

}
}

#endif


