#ifndef HTTP3_QPACK_DYNAMIC_TABLE
#define HTTP3_QPACK_DYNAMIC_TABLE

#include <deque>
#include <utility>
#include <cstdint>
#include <unordered_map>
#include "http3/qpack/type.h"
#include "http3/qpack/util.h"

namespace quicx {
namespace http3 {

class DynamicTable {
public:
    DynamicTable(uint32_t max_size);
    virtual ~DynamicTable();

    // Add a new header item to dynamic table
    // Returns the index of the added item, or -1 if failed
    bool AddHeaderItem(const std::string& name, const std::string& value);

    // Get header item by deque position (0 = newest). Used internally.
    HeaderItem* FindHeaderItem(uint32_t index);

    // Get header item by absolute index (RFC 9204 §3.2.4).
    // Absolute index = total_insert_count at insertion time - 1.
    // Converts to deque position internally.
    HeaderItem* FindHeaderItemByAbsoluteIndex(uint64_t absolute_index);

    // Get header item deque position by name and value.
    // Returns -1 if not found. NOTE: returns deque position, NOT absolute index.
    int32_t FindHeaderItemIndex(const std::string& name, const std::string& value);

    // Find absolute index (RFC 9204) by name and value.
    // Returns -1 if not found. Absolute index = total_insert_count_ - 1 - deque_position.
    int64_t FindAbsoluteIndex(const std::string& name, const std::string& value);

    // Get header item index by name only (first match), returns deque position or -1
    int32_t FindHeaderNameIndex(const std::string& name);

    // Find absolute index by name only (first match), returns -1 if not found
    int64_t FindAbsoluteNameIndex(const std::string& name);

    // Evict entries to ensure table size doesn't exceed max_size_
    void EvictEntries();

    // Get current size of dynamic table
    uint32_t GetTableSize() const { return current_size_; }

    // Get maximum allowed size of dynamic table
    uint32_t GetMaxTableSize() const { return max_size_; }
    // Get current entry count in table
    uint32_t GetEntryCount() const { return static_cast<uint32_t>(headeritem_deque_.size()); }

    // Get total number of inserts (monotonically increasing, used for RIC encoding)
    uint64_t GetInsertCount() const { return total_insert_count_; }

    // Update maximum size of dynamic table
    void UpdateMaxTableSize(uint32_t new_size);
    
    // RFC 9204 Section 4.3.4: Duplicate an existing entry
    // Duplicates the entry at the given absolute index
    // Returns true if successful, false if index is invalid
    bool DuplicateEntry(uint32_t absolute_index);

private:
    // Calculate size of a header entry (per RFC 7541 Section 4.1)
    uint32_t CalculateEntrySize(const std::string& name, const std::string& value);

    std::deque<HeaderItem> headeritem_deque_;  // Dynamic table entries (front=newest, back=oldest)
    std::unordered_map<std::pair<std::string, std::string>, uint32_t, pair_hash> headeritem_index_map_;

    uint32_t max_size_;      // Maximum allowed size of dynamic table
    uint32_t current_size_;  // Current size of dynamic table
    uint64_t total_insert_count_;  // Total number of inserts (monotonically increasing)
};

}
}

#endif
