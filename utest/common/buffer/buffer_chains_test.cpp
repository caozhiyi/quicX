#include <gtest/gtest.h>
#include "common/alloter/pool_block.h"
#include "common/buffer/buffer_block.h"
#include "common/buffer/buffer_chains.h"

namespace quicx {
namespace {

TEST(buffer_chains_utest, read) {
    auto alloter = MakeBlockMemoryPoolPtr(16, 5);
    std::shared_ptr<BufferChains> buffer_chains = std::make_shared<BufferChains>(alloter);

    uint8_t data[] = "123456789";
    uint32_t data_len = sizeof(data);
    for (size_t i = 0; i < 5; i++) {
        EXPECT_EQ(buffer_chains->Write(data, data_len), data_len);
    }
    
    EXPECT_EQ(buffer_chains->GetDataLength(), data_len * 5);

    uint8_t read_buf[64] = {};
    EXPECT_EQ(buffer_chains->Read(read_buf, 2), 2);
    EXPECT_EQ(buffer_chains->GetDataLength(), data_len * 5 - 2);

    auto buffers = buffer_chains->GetReadBuffers();
    uint32_t size = 0;
    for (; buffers; buffers = buffers->GetNext()) {
        size++;
    }
    EXPECT_EQ(size, 4);

    EXPECT_EQ(buffer_chains->ReadNotMovePt(read_buf, 2), 2);
    EXPECT_EQ(buffer_chains->GetDataLength(), data_len * 5 - 2);

    EXPECT_EQ(buffer_chains->MoveReadPt(2), 2);
    EXPECT_EQ(buffer_chains->Read(read_buf, 64), data_len * 5 - 4);
}

TEST(buffer_chains_utest, write) {
    auto alloter = MakeBlockMemoryPoolPtr(16, 5);
    std::shared_ptr<BufferChains> buffer_chains = std::make_shared<BufferChains>(alloter);

    uint8_t data[] = "123456789";
    uint32_t data_len = sizeof(data);
    for (size_t i = 0; i < 5; i++) {
        EXPECT_EQ(buffer_chains->Write(data, data_len), data_len);
    }
    
    EXPECT_EQ(buffer_chains->GetDataLength(), data_len * 5);

    auto buffers = buffer_chains->GetWriteBuffers(60);
    uint32_t size = 0;
    for (; buffers; buffers = buffers->GetNext()) {
        size++;
    }
    EXPECT_EQ(size, 4);

    EXPECT_EQ(buffer_chains->MoveWritePt(60), 60);
    EXPECT_EQ(buffer_chains->GetFreeLength(), 2);
}

}
}