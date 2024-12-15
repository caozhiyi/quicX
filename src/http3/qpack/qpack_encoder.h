#ifndef HTTP3_QPACK_QPACK_ENCODER
#define HTTP3_QPACK_QPACK_ENCODER

#include <memory>
#include <string>
#include <unordered_map>
#include "http3/qpack/dynamic_table.h"
#include "common/buffer/if_buffer_read.h"
#include "common/buffer/if_buffer_write.h"

namespace quicx {
namespace http3 {

class QpackEncoder {
public:
    QpackEncoder(): dynamic_table_(1024) {}
    ~QpackEncoder() {}

    bool Encode(const std::unordered_map<std::string, std::string>& headers, std::shared_ptr<common::IBufferWrite> buffer);
    bool Decode(const std::shared_ptr<common::IBufferRead> buffer, std::unordered_map<std::string, std::string>& headers);

private:
    void EncodeString(const std::string& str, std::shared_ptr<common::IBufferWrite> buffer);
    bool DecodeString(const std::shared_ptr<common::IBufferRead> buffer, std::string& output);

private:
    DynamicTable dynamic_table_;
};

}
}

#endif
