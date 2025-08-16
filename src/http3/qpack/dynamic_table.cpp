#include "http3/qpack/dynamic_table.h"

namespace quicx {
namespace http3 {

DynamicTable::DynamicTable(uint32_t max_size) : max_size_(max_size), current_size_(0) {

}

DynamicTable::~DynamicTable() {

}

bool DynamicTable::AddHeaderItem(const std::string& name, const std::string& value) {
    auto item_key = std::make_pair(name, value);
    if (headeritem_index_map_.count(item_key) > 0) {
        return true; // Entry already exists in table
    }
    
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

    // Update index mapping
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
    max_size_ = new_size;
    while (current_size_ > max_size_) {
        EvictEntries();
    }
}

uint32_t DynamicTable::CalculateEntrySize(const std::string& name, const std::string& value) {
    // Per RFC 7541 Section 4.1:
    // The size of an entry is the sum of its name's length in bytes, its value's
    // length in bytes, and 32 bytes (for overhead)
    return name.length() + value.length() + 32;
}

}
}
