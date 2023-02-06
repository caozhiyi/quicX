#include <gtest/gtest.h>
#include "common/alloter/pool_block.h"
#include "common/buffer/buffer_read_write.h"

namespace quicx {
namespace {
    
TEST(buffer_readonly_utest, buffer_readonly) {
    auto block = quicx::MakeBlockMemoryPoolPtr(63, 2);
    std::shared_ptr<BufferReadWrite> buffer = std::make_shared<BufferReadWrite>(block);
    const char* str = "it is a test str";
    auto pos_pair = buffer->GetWritePair();
    memcpy(pos_pair.first, str, sizeof(str));

    buffer->MoveWritePt(strlen(str));

    uint8_t ret_str[20] = {0};
    auto ret_size = buffer->Read(ret_str, 20);
}

}
}
