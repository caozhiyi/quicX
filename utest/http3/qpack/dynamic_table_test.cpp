// dynamic_table_test.cpp
#include <gtest/gtest.h>
#include "http3/qpack/dynamic_table.h"

namespace quicx {
namespace http3 {
namespace {

class DynamicTableTest : public testing::Test {
protected:
    void SetUp() override {
        table_ = std::make_unique<DynamicTable>(1024);
    }

    std::unique_ptr<DynamicTable> table_;
};

TEST_F(DynamicTableTest, InsertAndLookup) {
    // Insert a header field
    EXPECT_TRUE(table_->AddHeaderItem("content-type", "application/json"));
    
    // Look up the inserted field
    auto item = table_->FindHeaderItem(1);
    EXPECT_EQ(item->_name, "content-type");
    EXPECT_EQ(item->_value, "application/json");
}

TEST_F(DynamicTableTest, InsertMultipleEntries) {
    EXPECT_TRUE(table_->AddHeaderItem("content-type", "application/json"));
    EXPECT_TRUE(table_->AddHeaderItem("cache-control", "no-cache"));
    EXPECT_TRUE(table_->AddHeaderItem("accept", "*/*"));

    // Verify all entries can be looked up correctly
    auto item = table_->FindHeaderItem(1);
    EXPECT_EQ(item->_name, "accept");
    EXPECT_EQ(item->_value, "*/*");

    item = table_->FindHeaderItem(2);
    EXPECT_EQ(item->_name, "cache-control");
    EXPECT_EQ(item->_value, "no-cache");

    item = table_->FindHeaderItem(3);
    EXPECT_EQ(item->_name, "content-type");
    EXPECT_EQ(item->_value, "application/json");
}

TEST_F(DynamicTableTest, CapacityEviction) {
    // Set small capacity to test eviction
    table_->UpdateMaxTableSize(100);

    // Insert enough entries to trigger eviction
    EXPECT_TRUE(table_->AddHeaderItem("header1", "value1")); // ~32 bytes
    EXPECT_TRUE(table_->AddHeaderItem("header2", "value2")); // ~32 bytes
    EXPECT_TRUE(table_->AddHeaderItem("header3", "value3")); // ~32 bytes
    EXPECT_TRUE(table_->AddHeaderItem("header4", "value4")); // ~32 bytes

    // Verify old entries are evicted
    auto item = table_->FindHeaderItem(1);
    EXPECT_EQ(item->_name, "header1");
    EXPECT_EQ(item->_value, "value1");
    item = table_->FindHeaderItem(2);
    EXPECT_EQ(item->_name, "header2");
    EXPECT_EQ(item->_value, "value2");
    item = table_->FindHeaderItem(3);
    EXPECT_EQ(item->_name, "header3");
    EXPECT_EQ(item->_value, "value3");
    item = table_->FindHeaderItem(4);
    EXPECT_EQ(item->_name, "header4");
    EXPECT_EQ(item->_value, "value4");
}

TEST_F(DynamicTableTest, ResizeTable) {
    // Initial insertions
    EXPECT_TRUE(table_->AddHeaderItem("header1", "value1"));
    EXPECT_TRUE(table_->AddHeaderItem("header2", "value2"));

    // Reduce table size
    table_->UpdateMaxTableSize(50);

    // Verify entries are evicted
    auto item = table_->FindHeaderItem(2);
    EXPECT_EQ(item->_name, "header1");
    EXPECT_EQ(item->_value, "value1");
    item = table_->FindHeaderItem(1);
    EXPECT_EQ(item->_name, "header2");
    EXPECT_EQ(item->_value, "value2");

    // Increase table size
    table_->UpdateMaxTableSize(200);
    EXPECT_TRUE(table_->AddHeaderItem("header3", "value3")); // Should be able to insert new entry
}

TEST_F(DynamicTableTest, DuplicateEntries) {
    // Insert duplicate header fields
    EXPECT_TRUE(table_->AddHeaderItem("content-type", "application/json"));
    EXPECT_TRUE(table_->AddHeaderItem("content-type", "application/json"));

    // Verify both entries exist and are in correct order
    auto item = table_->FindHeaderItem(1);
    EXPECT_EQ(item->_name, "content-type");
    EXPECT_EQ(item->_value, "application/json");
    item = table_->FindHeaderItem(2);
    EXPECT_EQ(item->_name, "content-type");
    EXPECT_EQ(item->_value, "application/json");
}

TEST_F(DynamicTableTest, InvalidLookup) {
    EXPECT_TRUE(table_->AddHeaderItem("header1", "value1"));

    std::string name, value;
    // Try to look up invalid indices
    EXPECT_EQ(table_->FindHeaderItem(0), nullptr);  // Index 0 is invalid
    EXPECT_EQ(table_->FindHeaderItem(2), nullptr);  // Index 2 doesn't exist
    EXPECT_EQ(table_->FindHeaderItem(100), nullptr); // Large index doesn't exist
}

TEST_F(DynamicTableTest, EmptyStrings) {
    // Test empty names and values
    EXPECT_TRUE(table_->AddHeaderItem("", "value"));
    EXPECT_TRUE(table_->AddHeaderItem("name", ""));
    EXPECT_TRUE(table_->AddHeaderItem("", ""));

    auto item = table_->FindHeaderItem(1);
    EXPECT_TRUE(item->_name.empty());
    EXPECT_TRUE(item->_value.empty());
}

TEST_F(DynamicTableTest, SizeCalculation) {
    // Verify table size calculation
    EXPECT_TRUE(table_->AddHeaderItem("test", "value")); // 32 bytes overhead + 9 bytes content
    EXPECT_EQ(table_->GetTableSize(), 41);

    EXPECT_TRUE(table_->AddHeaderItem("longer-header", "longer-value")); // 32 + 23 = 55 bytes
    EXPECT_EQ(table_->GetTableSize(), 96); // 41 + 55 = 96 bytes
}

}  // namespace
}  // namespace http3
}  // namespace quicx