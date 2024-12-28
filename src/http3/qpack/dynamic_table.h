#ifndef HTTP3_QPACK_DYNAMIC_TABLE
#define HTTP3_QPACK_DYNAMIC_TABLE

#include <vector>
#include <utility>
#include <functional>
#include <unordered_map>
#include "http3/qpack/type.h"
#include "http3/qpack/util.h"

namespace quicx {
namespace http3 {

class DynamicTable {
public:
    DynamicTable(uint32_t max_size);
    virtual ~DynamicTable() {}

    // Add a new header item to dynamic table
    // Returns the index of the added item, or -1 if failed
    bool AddHeaderItem(const std::string& name, const std::string& value);

    // Get header item by absolute index
    HeaderItem* FindHeaderItem(uint32_t index);

    // Get header item index by name and value
    // Returns -1 if not found
    int32_t FindHeaderItemIndex(const std::string& name, const std::string& value);

    // Evict entries to ensure table size doesn't exceed max_size_
    void EvictEntries();

    // Get current size of dynamic table
    uint32_t GetTableSize() const { return current_size_; }

    // Get maximum allowed size of dynamic table
    uint32_t GetMaxTableSize() const { return max_size_; }

    // Update maximum size of dynamic table
    void UpdateMaxTableSize(uint32_t new_size);

private:
    // Calculate size of a header entry (per RFC 7541 Section 4.1)
    uint32_t CalculateEntrySize(const std::string& name, const std::string& value);

    std::vector<HeaderItem> headeritem_vec_;  // Dynamic table entries
    // TODO dynamic table support for duplicate entries
    std::unordered_map<std::pair<std::string, std::string>, uint32_t, pair_hash> headeritem_index_map_;

    uint32_t max_size_;      // Maximum allowed size of dynamic table
    uint32_t current_size_;  // Current size of dynamic table
};

}
}

#endif
