#include "http3/qpack/dynamic_table.h"

namespace quicx {
namespace http3 {

DynamicTable::DynamicTable(uint32_t max_size) : max_size_(max_size), current_size_(0), total_insert_count_(0) {

}

DynamicTable::~DynamicTable() {

}

bool DynamicTable::AddHeaderItem(const std::string& name, const std::string& value) {
    // Allow duplicate entries per QPACK (Duplicate instruction). Do not de-duplicate by name/value.
    uint32_t entry_size = CalculateEntrySize(name, value);
    
    // Check if new entry would exceed max size
    if (entry_size > max_size_) {
        return false;
    }

    // Evict entries if needed to make room
    while (current_size_ + entry_size > max_size_) {
        EvictEntries();
    }

    // Incremental index update: shift all existing indices by +1
    // since we push_front and indices are relative positions in deque
    for (auto& kv : headeritem_index_map_) {
        kv.second += 1;
    }

    // Add new entry at front (newest) with index 0
    headeritem_deque_.push_front(HeaderItem(name, value));
    current_size_ += entry_size;
    total_insert_count_++;

    // Insert (or update) the new entry's index to 0
    headeritem_index_map_[{name, value}] = 0;

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

HeaderItem* DynamicTable::FindHeaderItemByAbsoluteIndex(uint64_t absolute_index) {
    // BUGFIX P1-1: Correct absolute-to-deque conversion.
    // absolute_index = total_insert_count_ - 1 - deque_position  (at insertion time)
    // But after evictions, the deque shrinks from the back.
    // evicted_count = total_insert_count_ - deque.size()
    // deque_position = absolute_index - evicted_count
    //                = absolute_index - (total_insert_count_ - deque.size())
    uint64_t evicted_count = total_insert_count_ - headeritem_deque_.size();
    if (absolute_index < evicted_count) {
        return nullptr;  // Entry was evicted
    }
    // deque_position: newest entry has the highest absolute index and deque position 0
    // absolute_index of front (newest) = total_insert_count_ - 1
    // absolute_index of back (oldest)  = evicted_count
    // deque_position = (total_insert_count_ - 1) - absolute_index
    uint64_t deque_pos = (total_insert_count_ - 1) - absolute_index;
    if (deque_pos >= headeritem_deque_.size()) {
        return nullptr;
    }
    return &headeritem_deque_[static_cast<size_t>(deque_pos)];
}

int64_t DynamicTable::FindAbsoluteIndex(const std::string& name, const std::string& value) {
    int32_t deque_pos = FindHeaderItemIndex(name, value);
    if (deque_pos < 0) {
        return -1;
    }
    // absolute_index = total_insert_count_ - 1 - deque_position
    return static_cast<int64_t>(total_insert_count_) - 1 - deque_pos;
}

int32_t DynamicTable::FindHeaderNameIndex(const std::string& name) {
    for (uint32_t i = 0; i < headeritem_deque_.size(); ++i) {
        if (headeritem_deque_[i].name_ == name) return static_cast<int32_t>(i);
    }
    return -1;
}

int64_t DynamicTable::FindAbsoluteNameIndex(const std::string& name) {
    int32_t deque_pos = FindHeaderNameIndex(name);
    if (deque_pos < 0) {
        return -1;
    }
    return static_cast<int64_t>(total_insert_count_) - 1 - deque_pos;
}

void DynamicTable::EvictEntries() {
    if (headeritem_deque_.empty()) {
        return;
    }

    // Remove oldest entry (back of deque)
    const HeaderItem& item = headeritem_deque_.back();
    uint32_t evicted_index = static_cast<uint32_t>(headeritem_deque_.size() - 1);

    // Incremental index update: only remove the evicted entry from the map
    // if the map entry points to the evicted index (it could point to a newer duplicate)
    auto it = headeritem_index_map_.find({item.name_, item.value_});
    if (it != headeritem_index_map_.end() && it->second == evicted_index) {
        headeritem_index_map_.erase(it);
    }

    current_size_ -= CalculateEntrySize(item.name_, item.value_);
    headeritem_deque_.pop_back();
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
    
    // Copy name and value BEFORE calling AddHeaderItem, because AddHeaderItem
    // may call EvictEntries which could pop_back the entry we're referencing,
    // causing a dangling reference.
    std::string name = headeritem_deque_[absolute_index].name_;
    std::string value = headeritem_deque_[absolute_index].value_;
    
    return AddHeaderItem(name, value);
}

uint32_t DynamicTable::CalculateEntrySize(const std::string& name, const std::string& value) {
    // Per RFC 7541 Section 4.1:
    // The size of an entry is the sum of its name's length in bytes, its value's
    // length in bytes, and 32 bytes (for overhead)
    return name.length() + value.length() + 32;
}

}
}
