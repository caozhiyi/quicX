#ifndef HTTP3_QPACK_STATIC_TABLE
#define HTTP3_QPACK_STATIC_TABLE

#include <vector>
#include <utility> // for std::pair
#include <functional> // for std::hash
#include <unordered_map>
#include "http3/qpack/type.h"
#include "http3/qpack/util.h"
#include "common/util/singleton.h"

namespace quicx {
namespace http3 {

class StaticTable:
    public common::Singleton<StaticTable> {
public:
    StaticTable();
    virtual ~StaticTable() {}

    // Get header item by absolute index
    HeaderItem* FindHeaderItem(uint32_t index);

    // Get header item index by name and value
    // return -1 if not found
    int32_t FindHeaderItemIndex(const std::string& name, const std::string& value);

    // Get header item index by name
    // return -1 if not found
    int32_t FindHeaderItemIndex(const std::string& name);

private:
    std::vector<HeaderItem> headeritem_vec_; // static table
    std::unordered_map<std::string, uint32_t> headeritem_name_map_; // static table name map
    std::unordered_map<std::pair<std::string, std::string>, uint32_t, pair_hash> headeritem_index_map_; // static table index map
};

}
}

#endif
