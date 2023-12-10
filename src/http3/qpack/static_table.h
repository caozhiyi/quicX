#ifndef HTTP3_QPACK_STATIC_TABLE
#define HTTP3_QPACK_STATIC_TABLE

#include <vector>
#include <unordered_map>
#include "http3/qpack/type.h"

namespace quicx {
namespace http3 {

class StaticTable {
public:
    StaticTable();
    virtual ~StaticTable() {}

    HeaderItem* GetHeaderItem(uint32_t index);
    int32_t GetHeaderItem(const std::string& name, const std::string& value);
private:
    static std::vector<HeaderItem> _headeritem_vec;
    static std::unordered_map<std::string, uint32_t> _headeritem_index_map;
};

}
}

#endif
