// static_table_test.cpp
#include <gtest/gtest.h>
#include "http3/qpack/static_table.h"

namespace quicx {
namespace http3 {
namespace {

class StaticTableTest : public testing::Test {
protected:
    void SetUp() override {
        table_ = std::make_unique<StaticTable>();
    }

    std::unique_ptr<StaticTable> table_;
};

TEST_F(StaticTableTest, LookupByIndex) {
    std::string name, value;
    
    // Test lookup of known static entries
    auto item = table_->FindHeaderItem(1);
    EXPECT_TRUE(item != nullptr);
    EXPECT_EQ(item->name_, ":authority");
    EXPECT_EQ(item->value_, "");

    item = table_->FindHeaderItem(15);
    EXPECT_TRUE(item != nullptr);
    EXPECT_EQ(item->name_, ":method");
    EXPECT_EQ(item->value_, "CONNECT");
}

TEST_F(StaticTableTest, LookupInvalidIndex) {
    // Test invalid indices
    EXPECT_EQ(table_->FindHeaderItem(0), nullptr);  // Index 0 is invalid
    EXPECT_EQ(table_->FindHeaderItem(1000), nullptr);  // Index too large
}

TEST_F(StaticTableTest, LookupByNameAndValue) {
    // Test lookup of complete header field
    auto item = table_->FindHeaderItem(17);
    EXPECT_TRUE(item != nullptr);
    EXPECT_EQ(item->name_, ":method");
    EXPECT_EQ(item->value_, "GET");

    item = table_->FindHeaderItem(31);
    EXPECT_TRUE(item != nullptr);
    EXPECT_EQ(item->name_, "accept-encoding");
    EXPECT_EQ(item->value_, "gzip, deflate, br");
    
    // Test non-existent entries
    EXPECT_EQ(table_->FindHeaderItemIndex("non-existent", "value"), -1);
    EXPECT_EQ(table_->FindHeaderItemIndex(":method", "INVALID"), -1);
}

TEST_F(StaticTableTest, LookupByNameOnly) {
    // Test lookup by name only
    uint32_t index = table_->FindHeaderItemIndex(":method");
    EXPECT_EQ(index, 17);
    auto item = table_->FindHeaderItem(index);
    EXPECT_EQ(item->name_, ":method");
    EXPECT_EQ(item->value_, "GET");

    index = table_->FindHeaderItemIndex("accept-encoding");
    EXPECT_EQ(index, 31);
    item = table_->FindHeaderItem(index);
    EXPECT_EQ(item->name_, "accept-encoding");
    EXPECT_EQ(item->value_, "gzip, deflate, br");
    
    // Test non-existent name
    EXPECT_EQ(table_->FindHeaderItemIndex("non-existent"), -1);
}

}  // namespace
}  // namespace http3
}  // namespace quicx
