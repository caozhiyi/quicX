#ifndef HTTP3_QPACK_QPACK_ENCODER
#define HTTP3_QPACK_QPACK_ENCODER

#include <memory>
#include <string>
#include <functional>
#include <unordered_map>

#include "common/buffer/if_buffer.h"
#include "http3/qpack/dynamic_table.h"

namespace quicx {
namespace http3 {

class QpackEncoder {
public:
    QpackEncoder(): dynamic_table_(1024), max_table_capacity_(1024), enable_dynamic_table_(false) {}
    ~QpackEncoder() {}
    
    // Set the maximum table capacity from SETTINGS_QPACK_MAX_TABLE_CAPACITY
    void SetMaxTableCapacity(uint32_t max_capacity) { max_table_capacity_ = max_capacity; }
    uint32_t GetMaxTableCapacity() const { return max_table_capacity_; }
    
    // Enable or disable dynamic table usage (default: enabled for better compression)
    void SetDynamicTableEnabled(bool enabled) { enable_dynamic_table_ = enabled; }
    bool IsDynamicTableEnabled() const { return enable_dynamic_table_; }

    bool Encode(const std::unordered_map<std::string, std::string>& headers, std::shared_ptr<common::IBuffer> buffer);
    bool Decode(const std::shared_ptr<common::IBuffer> buffer, std::unordered_map<std::string, std::string>& headers);
    // Set a callback used to send encoder instructions (Insert entries) on QPACK encoder stream
    void SetInstructionSender(std::function<void(const std::vector<std::pair<std::string,std::string>>&)> cb) { instruction_sender_ = std::move(cb); }
    // Optional: set a function to emit decoder stream frames (Section Ack, Stream Cancel, Insert Count Increment)
    void SetDecoderFeedbackSender(std::function<void(uint8_t type, uint64_t value)> cb) { decoder_feedback_sender_ = std::move(cb); }
    // Emit a decoder feedback frame via bound sender
    void EmitDecoderFeedback(uint8_t type, uint64_t value) { if (decoder_feedback_sender_) decoder_feedback_sender_(type, value); }
    // Decoder-stream side: parse QPACK encoder instructions (RFC 9204)
    bool DecodeEncoderInstructions(const std::shared_ptr<common::IBuffer> instr_buf);
    // Encoder-stream side: generate QPACK encoder instructions (RFC 9204)
    bool EncodeEncoderInstructions(const std::vector<std::pair<std::string,std::string>>& inserts,
                                   std::shared_ptr<common::IBuffer> instr_buf,
                                   bool with_name_ref = false,
                                   bool set_capacity = false,
                                   uint32_t new_capacity = 0,
                                   int32_t duplicate_index = -1);
    // HEADERS prefix write/read per RFC 9204
    void WriteHeaderPrefix(std::shared_ptr<common::IBuffer> buffer, uint64_t required_insert_count, int64_t base);
    bool ReadHeaderPrefix(const std::shared_ptr<common::IBuffer> buffer, uint64_t& required_insert_count, int64_t& base);

private:
    void SetEnableDynamicTable(bool enable) { enable_dynamic_table_ = enable; }
    // Write Required Insert Count and Base per RFC 9204 §4.5; here we set simple values for demo
    void WritePrefix(std::shared_ptr<common::IBuffer> buffer, uint64_t required_insert_count, uint64_t base);
    bool ReadPrefix(const std::shared_ptr<common::IBuffer> buffer, uint64_t& required_insert_count, uint64_t& base);
    void EncodeString(const std::string& str, std::shared_ptr<common::IBuffer> buffer);
    bool DecodeString(const std::shared_ptr<common::IBuffer> buffer, std::string& output);

private:
    DynamicTable dynamic_table_;
    uint32_t max_table_capacity_;  // Maximum table capacity from SETTINGS_QPACK_MAX_TABLE_CAPACITY
    // Dynamic table enabled by default for better compression (RFC 9204)
    // Can be disabled via SetDynamicTableEnabled() if needed
    bool enable_dynamic_table_ {true};
    std::function<void(const std::vector<std::pair<std::string,std::string>>&)> instruction_sender_;
    std::function<void(uint8_t type, uint64_t value)> decoder_feedback_sender_;
};

}
}

#endif
