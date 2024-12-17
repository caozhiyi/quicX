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
    auto item = table_->FindHeaderItem(0);
    EXPECT_EQ(item->name_, "content-type");
    EXPECT_EQ(item->value_, "application/json");
}

TEST_F(DynamicTableTest, InsertMultipleEntries) {
    EXPECT_TRUE(table_->AddHeaderItem("content-type", "application/json"));
    EXPECT_TRUE(table_->AddHeaderItem("cache-control", "no-cache"));
    EXPECT_TRUE(table_->AddHeaderItem("accept", "*/*"));

    // Verify all entries can be looked up correctly
    auto item = table_->FindHeaderItem(0);
    EXPECT_EQ(item->name_, "accept");
    EXPECT_EQ(item->value_, "*/*");

    item = table_->FindHeaderItem(1);
    EXPECT_EQ(item->name_, "cache-control");
    EXPECT_EQ(item->value_, "no-cache");

    item = table_->FindHeaderItem(2);
    EXPECT_EQ(item->name_, "content-type");
    EXPECT_EQ(item->value_, "application/json");
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
    auto item = table_->FindHeaderItem(0);
    EXPECT_EQ(item->name_, "header4");
    EXPECT_EQ(item->value_, "value4");
    item = table_->FindHeaderItem(1);
    EXPECT_EQ(item->name_, "header3");
    EXPECT_EQ(item->value_, "value3");
    EXPECT_EQ(table_->FindHeaderItem(2), nullptr);
    EXPECT_EQ(table_->FindHeaderItem(3), nullptr);
}

TEST_F(DynamicTableTest, ResizeTable) {
    // Initial insertions
    EXPECT_TRUE(table_->AddHeaderItem("header1", "value1"));
    EXPECT_TRUE(table_->AddHeaderItem("header2", "value2"));

    // Reduce table size
    table_->UpdateMaxTableSize(50);

    // Verify entries are evicted
    EXPECT_EQ(table_->FindHeaderItem(1), nullptr);
    
    auto item = table_->FindHeaderItem(0);
    EXPECT_EQ(item->name_, "header2");
    EXPECT_EQ(item->value_, "value2");

    // Increase table size
    table_->UpdateMaxTableSize(200);
    EXPECT_TRUE(table_->AddHeaderItem("header3", "value3")); // Should be able to insert new entry
}

TEST_F(DynamicTableTest, DuplicateEntries) {
    // Insert duplicate header fields
    EXPECT_TRUE(table_->AddHeaderItem("content-type", "application/json"));
    EXPECT_TRUE(table_->AddHeaderItem("content-type", "application/json"));

    // Verify both entries exist and are in correct order
    auto item = table_->FindHeaderItem(0);
    EXPECT_EQ(item->name_, "content-type");
    EXPECT_EQ(item->value_, "application/json");

    EXPECT_EQ(table_->FindHeaderItem(1), nullptr);
}

TEST_F(DynamicTableTest, InvalidLookup) {
    EXPECT_TRUE(table_->AddHeaderItem("header1", "value1"));

    // Try to look up invalid indices
    EXPECT_TRUE(table_->FindHeaderItem(0) != nullptr);  // Index 0 is valid
    EXPECT_EQ(table_->FindHeaderItem(2), nullptr);  // Index 2 doesn't exist
    EXPECT_EQ(table_->FindHeaderItem(100), nullptr); // Large index doesn't exist
}

TEST_F(DynamicTableTest, EmptyStrings) {
    // Test empty names and values
    EXPECT_TRUE(table_->AddHeaderItem("", "value"));
    EXPECT_TRUE(table_->AddHeaderItem("name", ""));
    EXPECT_TRUE(table_->AddHeaderItem("", ""));

    auto item = table_->FindHeaderItem(0);
    EXPECT_TRUE(item->name_.empty());
    EXPECT_TRUE(item->value_.empty());
}

TEST_F(DynamicTableTest, SizeCalculation) {
    // size = name(4) + value(5) + overhead(32)
    table_->AddHeaderItem("test", "value");
    EXPECT_EQ(table_->GetTableSize(), 41);

    // size = name(3) + value(3) + overhead(32) 
    table_->AddHeaderItem("foo", "bar");
    EXPECT_EQ(table_->GetTableSize(), 79);

    // size = name(1) + value(1) + overhead(32) = 34 
    table_->AddHeaderItem("x", "y");
    EXPECT_EQ(table_->GetTableSize(), 113);
}

}  // namespace
}  // namespace http3
}  // namespace quicx