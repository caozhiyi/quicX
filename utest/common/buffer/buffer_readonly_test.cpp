#include <gtest/gtest.h>
#include "common/alloter/pool_block.h"
#include "common/buffer/buffer_readonly.h"

namespace quicx {
namespace {
    
TEST(buffer_readonly_utest, buffer_readonly) {
    auto block = quicx::MakeBlockMemoryPoolPtr(63, 2);
    std::shared_ptr<BufferReadWrite> buffer = std::make_shared<BufferReadWrite>(block);
    const char* str = "it is a test str";
    auto pos_pair = buffer->GetWritePair();
    strcpy(pos_pair.first, str);

    buffer->MoveWritePt(strlen(str));

    char ret_str[20] = {0};
    auto ret_size = buffer->Read(ret_str, 20);
}

}
}
