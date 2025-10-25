#include "http3/qpack/dynamic_table.h"

namespace quicx {
namespace http3 {

DynamicTable::DynamicTable(uint32_t max_size) : max_size_(max_size), current_size_(0) {

}

DynamicTable::~DynamicTable() {

}

bool DynamicTable::AddHeaderItem(const std::string& name, const std::string& value) {
    // Allow duplicate entries per QPACK (Duplicate instruction). Do not de-duplicate by name/value.
    auto item_key = std::make_pair(name, value);
    
    uint32_t entry_size = CalculateEntrySize(name, value);
    
    // Check if new entry would exceed max size
    if (entry_size > max_size_) {
        return false;
    }

    // Evict entries if needed to make room
    while (current_size_ + entry_size > max_size_) {
        EvictEntries();
    }

    // Add new entry at front (newest)
    headeritem_deque_.push_front(HeaderItem(name, value));
    current_size_ += entry_size;

    // Update index mapping (maps unique name/value to a representative index).
    // Note: With duplicates present, the map will point to the most recent index encountered in the loop.
    headeritem_index_map_.clear();
    for (uint32_t i = 0; i < headeritem_deque_.size(); i++) {
        const HeaderItem& item = headeritem_deque_[i];
        headeritem_index_map_[{item.name_, item.value_}] = i;
    }

    return true;
}

HeaderItem* DynamicTable::FindHeaderItem(uint32_t index) {
    if (index < headeritem_deque_.size()) {
        return &headeritem_deque_[index];
    }
    return nullptr;
}

int32_t DynamicTable::FindHeaderItemIndex(const std::string& name, const std::string& value) {
    auto iter = headeritem_index_map_.find({name, value});
    if (iter != headeritem_index_map_.end()) {
        return iter->second;
    }
    return -1;
}

int32_t DynamicTable::FindHeaderNameIndex(const std::string& name) {
    for (uint32_t i = 0; i < headeritem_deque_.size(); ++i) {
        if (headeritem_deque_[i].name_ == name) return static_cast<int32_t>(i);
    }
    return -1;
}

void DynamicTable::EvictEntries() {
    if (headeritem_deque_.empty()) {
        return;
    }

    // Remove oldest entry
    const HeaderItem& item = headeritem_deque_.back();
    current_size_ -= CalculateEntrySize(item.name_, item.value_);
    headeritem_deque_.pop_back();

    // Update index mapping
    headeritem_index_map_.clear();
    for (uint32_t i = 0; i < headeritem_deque_.size(); i++) {
        const HeaderItem& item2 = headeritem_deque_[i];
        headeritem_index_map_[{item2.name_, item2.value_}] = i;
    }
}

void DynamicTable::UpdateMaxTableSize(uint32_t new_size) {
    // RFC 9204 Section 3.2.3: Dynamic Table Capacity
    // The decoder MUST treat a new maximum size value that exceeds the limit 
    // set by SETTINGS_QPACK_MAX_TABLE_CAPACITY as a connection error
    
    // Note: This check should be done at a higher level (connection) where
    // SETTINGS_QPACK_MAX_TABLE_CAPACITY is known. Here we just update the size.
    // The caller is responsible for validating against the setting.
    
    max_size_ = new_size;
    
    // Evict entries if current size exceeds new max size
    while (current_size_ > max_size_) {
        EvictEntries();
    }
}

bool DynamicTable::DuplicateEntry(uint32_t absolute_index) {
    // RFC 9204 Section 4.3.4: Duplicate instruction
    // Duplicates an existing dynamic table entry by its absolute index
    
    if (absolute_index >= headeritem_deque_.size()) {
        return false; // Invalid index
    }
    
    // Get the entry to duplicate
    const HeaderItem& original = headeritem_deque_[absolute_index];
    
    // Add a copy of the entry (uses AddHeaderItem internally)
    return AddHeaderItem(original.name_, original.value_);
}

uint32_t DynamicTable::CalculateEntrySize(const std::string& name, const std::string& value) {
    // Per RFC 7541 Section 4.1:
    // The size of an entry is the sum of its name's length in bytes, its value's
    // length in bytes, and 32 bytes (for overhead)
    return name.length() + value.length() + 32;
}

}
}
