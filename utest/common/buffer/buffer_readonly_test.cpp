#include <gtest/gtest.h>
#include "common/buffer/buffer.h"
#include "common/alloter/pool_block.h"


namespace quicx {
namespace common {
namespace {
    
TEST(buffer_readonly_utest, buffer) {
    auto block = common::MakeBlockMemoryPoolPtr(63, 2);
    std::shared_ptr<Buffer> buffer = std::make_shared<Buffer>(block);
    const char* str = "it is a test str";
    auto span = buffer->GetWriteSpan();
    memcpy(span.GetStart(), str, sizeof(str));

    buffer->MoveWritePt(strlen(str));

    uint8_t ret_str[20] = {0};
    auto ret_size = buffer->Read(ret_str, 20);
}

}
}
}