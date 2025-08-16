#ifndef HTTP3_QPACK_QPACK_ENCODER
#define HTTP3_QPACK_QPACK_ENCODER

#include <memory>
#include <string>
#include <functional>
#include <unordered_map>

#include "http3/qpack/dynamic_table.h"
#include "common/buffer/if_buffer_read.h"
#include "common/buffer/if_buffer_write.h"

namespace quicx {
namespace http3 {

class QpackEncoder {
public:
    QpackEncoder(): dynamic_table_(1024) {}
    ~QpackEncoder() {}

    bool Encode(const std::unordered_map<std::string, std::string>& headers, std::shared_ptr<common::IBufferWrite> buffer);
    bool Decode(const std::shared_ptr<common::IBufferRead> buffer, std::unordered_map<std::string, std::string>& headers);
    // Set a callback used to send encoder instructions (Insert entries) on QPACK encoder stream
    void SetInstructionSender(std::function<void(const std::vector<std::pair<std::string,std::string>>&)> cb) { instruction_sender_ = std::move(cb); }
    // Optional: set a function to emit decoder stream frames (Section Ack, Stream Cancel, Insert Count Increment)
    void SetDecoderFeedbackSender(std::function<void(uint8_t type, uint64_t value)> cb) { decoder_feedback_sender_ = std::move(cb); }
    // Emit a decoder feedback frame via bound sender
    void EmitDecoderFeedback(uint8_t type, uint64_t value) { if (decoder_feedback_sender_) decoder_feedback_sender_(type, value); }
    // Decoder-stream side: parse QPACK encoder instructions (RFC 9204)
    bool DecodeEncoderInstructions(const std::shared_ptr<common::IBufferRead> instr_buf);
    // Encoder-stream side: generate QPACK encoder instructions (RFC 9204)
    bool EncodeEncoderInstructions(const std::vector<std::pair<std::string,std::string>>& inserts,
                                   std::shared_ptr<common::IBufferWrite> instr_buf,
                                   bool with_name_ref = false,
                                   bool set_capacity = false,
                                   uint32_t new_capacity = 0,
                                   int32_t duplicate_index = -1);
    // HEADERS prefix write/read per RFC 9204
    void WriteHeaderPrefix(std::shared_ptr<common::IBufferWrite> buffer, uint64_t required_insert_count, int64_t base);
    bool ReadHeaderPrefix(const std::shared_ptr<common::IBufferRead> buffer, uint64_t& required_insert_count, int64_t& base);

private:
    void SetEnableDynamicTable(bool enable) { enable_dynamic_table_ = enable; }
    // Write Required Insert Count and Base per RFC 9204 §4.5; here we set simple values for demo
    void WritePrefix(std::shared_ptr<common::IBufferWrite> buffer, uint64_t required_insert_count, uint64_t base);
    bool ReadPrefix(const std::shared_ptr<common::IBufferRead> buffer, uint64_t& required_insert_count, uint64_t& base);
    void EncodeString(const std::string& str, std::shared_ptr<common::IBufferWrite> buffer);
    bool DecodeString(const std::shared_ptr<common::IBufferRead> buffer, std::string& output);

private:
    DynamicTable dynamic_table_;
    // blocked header decoding when encoder instructions are not transported.
    bool enable_dynamic_table_ {false};
    std::function<void(const std::vector<std::pair<std::string,std::string>>&)> instruction_sender_;
    std::function<void(uint8_t type, uint64_t value)> decoder_feedback_sender_;
};

}
}

#endif
