#include <gtest/gtest.h>

#include "quic/packet/long_header.h"
#include "quic/frame/frame_interface.h"

#include "common/alloter/pool_block.h"
#include "common/buffer/buffer_queue.h"
#include "common/alloter/pool_alloter.h"

class TestLongHeader: public quicx::LongHeader {
public:
    TestLongHeader() {
        _header_format._header_info._fix_bit = 1;
        _header_format._header_info._fix_bit = 1;
        _header_format._header_info._packet_type = 3;
        _header_format._header_info._special_type = 4;

        _destination_connection_id_length = 2;
        strcpy(_destination_connection_id, "11");

        _source_connection_id_length = 2;
        strcpy(_destination_connection_id, "22");
    }
    ~TestLongHeader() {}

    virtual bool AddFrame(std::shared_ptr<quicx::IFrame> frame) { return true; }
};

TEST(long_header_utest, encode) {
    TestLongHeader long_header1 = TestLongHeader();
    TestLongHeader long_header2 = TestLongHeader();

    auto IAlloter = std::make_shared<quicx::AlloterWrap>(quicx::MakePoolAlloterPtr());
    auto block = quicx::MakeBlockMemoryPoolPtr(32, 2);
    auto buffer = std::make_shared<quicx::BufferQueue>(block, IAlloter);

    EXPECT_TRUE(long_header1.Encode(buffer, IAlloter));
    EXPECT_TRUE(long_header2.Decode(buffer, IAlloter));

    // TODO check result
}
