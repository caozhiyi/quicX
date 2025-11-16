#ifndef HTTP3_FRAME_QPACK_ENCODER_FRAMES
#define HTTP3_FRAME_QPACK_ENCODER_FRAMES

#include <string>
#include <cstdint>

#include "common/buffer/if_buffer.h"

namespace quicx {
namespace http3 {

enum class QpackEncoderType: uint8_t {
    kSetDynamicTableCapacity = 0x3f, // 00111111 + varint (RFC 9204 §4.3)
    kInsertWithNameRef       = 0x80, // 1 N T (pattern) - simplified here as type byte then fields
    kInsertWithoutNameRef    = 0x40, // 01 T ...
    kDuplicate               = 0x00, // 00000000 + varint index
};

class IQpackEncoderFrame {
public:
    IQpackEncoderFrame(uint8_t type = 0): type_(type) {}
    IQpackEncoderFrame(QpackEncoderType type): type_(static_cast<uint8_t>(type)) {}
    virtual ~IQpackEncoderFrame() = default;
    virtual QpackEncoderType GetType() { return static_cast<QpackEncoderType>(type_); }
    virtual bool Encode(std::shared_ptr<common::IBuffer> buffer) = 0;
    virtual bool Decode(std::shared_ptr<common::IBuffer> buffer) = 0;
    virtual uint32_t EvaluateEncodeSize() = 0;
protected:
    uint8_t type_;
};

// Set Dynamic Table Capacity: [t=0x3f][capacity varint]
// 0   1   2   3   4   5   6   7
// +---+---+---+---+---+---+---+---+
// | 0 | 0 | 1 |   Capacity (5+)   |
// +---+---+---+-------------------+
class QpackSetCapacityFrame:
    public IQpackEncoderFrame {
public:
    QpackSetCapacityFrame(uint8_t type = 0);
    QpackSetCapacityFrame(QpackEncoderType type);
    virtual bool Encode(std::shared_ptr<common::IBuffer> buffer) override;
    virtual bool Decode(std::shared_ptr<common::IBuffer> buffer) override;
    virtual uint32_t EvaluateEncodeSize() override { return 1; }

    uint64_t GetCapacity() const { return capacity_; }
    void SetCapacity(uint64_t capacity) { capacity_ = capacity; }
private:
    uint64_t capacity_;
};

// Insert With Name Reference (encoder stream): 1 S NPNNNNN (6-bit prefix index) + value string
// 0   1   2   3   4   5   6   7
// +---+---+---+---+---+---+---+---+
// | 1 | T |    Name Index (6+)    |
// +---+---+-----------------------+
// | H |     Value Length (7+)     |
// +---+---------------------------+
// |  Value String (Length bytes)  |
// +-------------------------------+
class QpackInsertWithNameRefFrame:
    public IQpackEncoderFrame {
public:
    QpackInsertWithNameRefFrame();
    bool Encode(std::shared_ptr<common::IBuffer> buffer) override;
    bool Decode(std::shared_ptr<common::IBuffer> buffer) override;
    uint32_t EvaluateEncodeSize() override;
    void Set(bool is_static, uint64_t name_index, const std::string& value);
    bool IsStatic() const;
    uint64_t GetNameIndex() const;
    const std::string& GetValue() const;
private:
    bool is_static_ = false;
    uint64_t name_index_ = 0;
    std::string value_;
};

// Insert Without Name Reference (encoder stream): 01NNNNNN (6-bit prefix zero) + name string + value string
// 0   1   2   3   4   5   6   7
// +---+---+---+---+---+---+---+---+
// | 0 | 1 | H | Name Length (5+)  |
// +---+---+---+-------------------+
// |  Name String (Length bytes)   |
// +---+---------------------------+
// | H |     Value Length (7+)     |
// +---+---------------------------+
// |  Value String (Length bytes)  |
// +-------------------------------+
class QpackInsertWithoutNameRefFrame:
    public IQpackEncoderFrame {
public:
    QpackInsertWithoutNameRefFrame();
    bool Encode(std::shared_ptr<common::IBuffer> buffer) override;
    bool Decode(std::shared_ptr<common::IBuffer> buffer) override;
    uint32_t EvaluateEncodeSize() override;
    void Set(const std::string& name, const std::string& value);
    const std::string& GetName() const;
    const std::string& GetValue() const;
private:
    std::string name_;
    std::string value_;
};

// Duplicate (encoder stream): 0001xxxx (4-bit prefix index)
// 0   1   2   3   4   5   6   7
// +---+---+---+---+---+---+---+---+
// | 0 | 0 | 0 |    Index (5+)     |
// +---+---+---+-------------------+
class QpackDuplicateFrame:
    public IQpackEncoderFrame {
public:
    QpackDuplicateFrame();
    bool Encode(std::shared_ptr<common::IBuffer> buffer) override;
    bool Decode(std::shared_ptr<common::IBuffer> buffer) override;
    uint32_t EvaluateEncodeSize() override;
    void Set(uint64_t idx);
    uint64_t Get() const;
private:
    uint64_t index_ = 0;
};

}
}

#endif


