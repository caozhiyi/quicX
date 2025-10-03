#include <gtest/gtest.h>

#include <memory>
#include <vector>
#include <unordered_map>

#include "http3/qpack/util.h"
#include "common/buffer/buffer.h"
#include "http3/qpack/qpack_encoder.h"

namespace quicx {
namespace http3 {
namespace {

using quicx::common::Buffer;

class QpackDynamicUpdateTest : public ::testing::Test {
protected:
    void SetUp() override {
        // fresh instances for each test
        decoder_ = std::make_unique<QpackEncoder>();
        tx_ = std::make_unique<QpackEncoder>();
    }

    // Helper to make a buffer with given capacity
    static std::shared_ptr<common::Buffer> MakeBuffer(size_t cap = 1024) {
        auto mem = std::make_shared<std::vector<uint8_t>>(cap);
        // Construct Buffer on provided memory
        return std::make_shared<Buffer>(mem->data(), static_cast<uint32_t>(mem->size()));
    }

    std::unique_ptr<QpackEncoder> decoder_;
    std::unique_ptr<QpackEncoder> tx_;
};

// Insert Without Name Reference, then decode a header block that indexes the dynamic table entry
TEST_F(QpackDynamicUpdateTest, InsertWithoutNameRef_ThenDecodeIndexedHeader) {
    auto ctrl = MakeBuffer();

    // Encoder stream: insert name-value without name reference
    std::vector<std::pair<std::string,std::string>> inserts = {{"x-custom", "alpha"}};
    ASSERT_TRUE(tx_->EncodeEncoderInstructions(inserts, ctrl));

    // Decoder side consumes encoder instructions to populate its dynamic table
    ASSERT_TRUE(decoder_->DecodeEncoderInstructions(ctrl));

    // Build a header block that references the newest dynamic entry (absolute index 0)
    // For one inserted entry: RIC=1, BASE=1 → relative index for newest = 0
    auto hdr = MakeBuffer();
    decoder_->WriteHeaderPrefix(hdr, /*required_insert_count*/1, /*base*/1);
    // Indexed Header Field — dynamic: first byte 10xxxxxx with 6-bit prefix, value = relative index
    ASSERT_TRUE(QpackEncodePrefixedInteger(hdr, 6, 0x80, /*relative_index*/0));

    std::unordered_map<std::string, std::string> headers;
    ASSERT_TRUE(decoder_->Decode(hdr, headers));
    ASSERT_EQ(headers.size(), 1u);
    EXPECT_EQ(headers["x-custom"], "alpha");
}

// Insert With Static Name Reference (e.g., ":method"), then decode
TEST_F(QpackDynamicUpdateTest, InsertWithStaticNameRef_ThenDecodeIndexedHeader) {
    auto ctrl = MakeBuffer();

    // Use a name present in static table, e.g., ":method"
    std::vector<std::pair<std::string,std::string>> inserts = {{":method", "GET"}};
    ASSERT_TRUE(tx_->EncodeEncoderInstructions(inserts, ctrl, /*with_name_ref*/true));

    ASSERT_TRUE(decoder_->DecodeEncoderInstructions(ctrl));

    // Generate header block referencing newest dynamic entry
    auto hdr = MakeBuffer();
    decoder_->WriteHeaderPrefix(hdr, 1, 1);
    ASSERT_TRUE(QpackEncodePrefixedInteger(hdr, 6, 0x80, 0));

    std::unordered_map<std::string, std::string> headers;
    ASSERT_TRUE(decoder_->Decode(hdr, headers));
    ASSERT_EQ(headers.size(), 1u);
    EXPECT_EQ(headers[":method"], "GET");
}

// Duplicate instruction creates a new dynamic entry which can also be referenced
TEST_F(QpackDynamicUpdateTest, DuplicateInstruction_CreatesSecondEntry) {
    auto ctrl = MakeBuffer();

    // Insert first entry
    std::vector<std::pair<std::string,std::string>> inserts = {{"x-dupe", "v"}};
    ASSERT_TRUE(tx_->EncodeEncoderInstructions(inserts, ctrl));
    ASSERT_TRUE(decoder_->DecodeEncoderInstructions(ctrl));

    // Issue Duplicate of the newest entry (relative index 0)
    auto ctrl2 = MakeBuffer();
    ASSERT_TRUE(tx_->EncodeEncoderInstructions(/*inserts*/{}, ctrl2, /*with_name_ref*/false,
                                               /*set_capacity*/false, /*new_capacity*/0,
                                               /*duplicate_index*/0));
    ASSERT_TRUE(decoder_->DecodeEncoderInstructions(ctrl2));

    // Now there should be two entries; build a header block that references both entries
    // With two inserts: RIC=2, BASE=2. Newest relative=0, next relative=1.
    auto hdr = MakeBuffer();
    decoder_->WriteHeaderPrefix(hdr, 2, 2);
    ASSERT_TRUE(QpackEncodePrefixedInteger(hdr, 6, 0x80, 0)); // newest
    ASSERT_TRUE(QpackEncodePrefixedInteger(hdr, 6, 0x80, 1)); // older duplicate

    std::unordered_map<std::string, std::string> headers;
    ASSERT_TRUE(decoder_->Decode(hdr, headers));
    // Both map to the same name, value; map will keep one entry with final value identical
    EXPECT_EQ(headers["x-dupe"], "v");
}

// If Required Insert Count exceeds dynamic table size, decoding is blocked (Decode returns false)
TEST_F(QpackDynamicUpdateTest, DecodeBlocked_WhenRICExceedsTable) {
    auto ctrl = MakeBuffer();

    // Only one insert
    std::vector<std::pair<std::string,std::string>> inserts = {{"x-block", "b"}};
    ASSERT_TRUE(tx_->EncodeEncoderInstructions(inserts, ctrl));
    ASSERT_TRUE(decoder_->DecodeEncoderInstructions(ctrl));

    // Build a header block requiring 2 inserts while only 1 is available
    auto hdr = MakeBuffer();
    decoder_->WriteHeaderPrefix(hdr, /*required_insert_count*/2, /*base*/2);
    // Even without any fields, decode should fail due to blocked state
    std::unordered_map<std::string, std::string> headers;
    EXPECT_FALSE(decoder_->Decode(hdr, headers));
}

} // namespace
} // namespace http3
} // namespace quicx
