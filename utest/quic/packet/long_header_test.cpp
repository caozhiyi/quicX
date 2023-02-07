#include <gtest/gtest.h>

#include "quic/frame/frame_interface.h"
#include "quic/packet/header/long_header.h"

#include "common/alloter/pool_block.h"

/*
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

    auto alloter = quicx::MakeBlockMemoryPoolPtr(128, 2);
    std::shared_ptr<BufferReadWrite> read_buffer = std::make_shared<BufferReadWrite>(alloter);
    std::shared_ptr<BufferReadWrite> write_buffer = std::make_shared<BufferReadWrite>(alloter);


    EXPECT_TRUE(long_header1.Encode(write_buffer));

    auto data_piar = write_buffer->GetReadPair();
    auto pos_piar = read_buffer->GetReadPair();
    memcpy(pos_piar.first, data_piar.first, data_piar.second - data_piar.first);
    read_buffer->MoveWritePt(data_piar.second - data_piar.first);
    EXPECT_TRUE(long_header2.Decode(read_buffer, true));

    // TODO check result
}
*/