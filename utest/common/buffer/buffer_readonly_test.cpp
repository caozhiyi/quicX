#include <gtest/gtest.h>
#include "common/buffer/buffer.h"
#include "common/alloter/pool_block.h"


namespace quicx {
namespace {
    
TEST(buffer_readonly_utest, buffer) {
    auto block = quicx::MakeBlockMemoryPoolPtr(63, 2);
    std::shared_ptr<Buffer> buffer = std::make_shared<Buffer>(block);
    const char* str = "it is a test str";
    auto pos_pair = buffer->GetWritePair();
    memcpy(pos_pair.first, str, sizeof(str));

    buffer->MoveWritePt(strlen(str));

    uint8_t ret_str[20] = {0};
    auto ret_size = buffer->Read(ret_str, 20);
}

}
}
