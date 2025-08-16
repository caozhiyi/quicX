#ifndef HTTP3_FRAME_QPACK_DECODER_FRAMES
#define HTTP3_FRAME_QPACK_DECODER_FRAMES

#include <cstdint>
#include "common/buffer/if_buffer_read.h"
#include "common/buffer/if_buffer_write.h"

namespace quicx {
namespace http3 {

enum class QpackDecoderInstrType : uint8_t {
    kSectionAck          = 0x00,
    kStreamCancellation  = 0x01,
    kInsertCountInc      = 0x02,
};

class IQpackDecoderFrame {
public:
    IQpackDecoderFrame() {}
    virtual ~IQpackDecoderFrame() = default;
    virtual bool Encode(std::shared_ptr<common::IBufferWrite> buffer) = 0;
    virtual bool Decode(std::shared_ptr<common::IBufferRead> buffer) = 0;
    virtual uint32_t EvaluateEncodeSize() = 0;
    virtual uint8_t GetType() const = 0;
};

bool DecodeQpackDecoderFrames(std::shared_ptr<common::IBufferRead> buffer, std::vector<std::shared_ptr<IQpackDecoderFrame>>& frames);

// Section Acknowledgement: [t=0x00][stream_id varint][section_number varint]
// 0   1   2   3   4   5   6   7
// +---+---+---+---+---+---+---+---+
// | 1 |      Stream ID (7+)       |
// +---+---------------------------+
class QpackSectionAckFrame:
    public IQpackDecoderFrame {
public:
    QpackSectionAckFrame();
    ~QpackSectionAckFrame() override = default;

    bool Encode(std::shared_ptr<common::IBufferWrite> buffer) override;
    bool Decode(std::shared_ptr<common::IBufferRead> buffer) override;
    uint32_t EvaluateEncodeSize() override;
    uint8_t GetType() const override { return static_cast<uint8_t>(QpackDecoderInstrType::kSectionAck); }

    uint64_t GetStreamId() const;
    uint64_t GetSectionNumber() const;
    void Set(uint64_t sid, uint64_t sec);

private:
    uint64_t stream_id_ = 0;
    uint64_t section_number_ = 0;
};

// Stream Cancellation: [t=0x01][stream_id varint][section_number varint]
// 0   1   2   3   4   5   6   7
// +---+---+---+---+---+---+---+---+
// | 0 | 1 |     Stream ID (6+)    |
// +---+---+-----------------------+
class QpackStreamCancellationFrame: public IQpackDecoderFrame {
public:
    QpackStreamCancellationFrame();
    ~QpackStreamCancellationFrame() override = default;

    bool Encode(std::shared_ptr<common::IBufferWrite> buffer) override;
    bool Decode(std::shared_ptr<common::IBufferRead> buffer) override;
    uint32_t EvaluateEncodeSize() override;
    uint8_t GetType() const override { return static_cast<uint8_t>(QpackDecoderInstrType::kStreamCancellation); }

    uint64_t GetStreamId() const;
    uint64_t GetSectionNumber() const;
    void Set(uint64_t sid, uint64_t sec);

private:
    uint64_t stream_id_ = 0;
    uint64_t section_number_ = 0;
};

// Insert Count Increment: [t=0x02][delta varint]
// 0   1   2   3   4   5   6   7
// +---+---+---+---+---+---+---+---+
// | 0 | 0 |     Increment (6+)    |
// +---+---+-----------------------+
class QpackInsertCountIncrementFrame: public IQpackDecoderFrame {
public:
    QpackInsertCountIncrementFrame();
    ~QpackInsertCountIncrementFrame() override = default;

    bool Encode(std::shared_ptr<common::IBufferWrite> buffer) override;
    bool Decode(std::shared_ptr<common::IBufferRead> buffer) override;
    uint32_t EvaluateEncodeSize() override;
    uint8_t GetType() const override { return static_cast<uint8_t>(QpackDecoderInstrType::kInsertCountInc); }

    uint64_t GetDelta() const;
    void Set(uint64_t d);

private:
    uint64_t delta_ = 0;
};

}
}

#endif


