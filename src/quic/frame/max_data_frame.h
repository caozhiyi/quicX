#ifndef QUIC_FRAME_MAX_DATA_FRAME
#define QUIC_FRAME_MAX_DATA_FRAME

#include <cstdint>
#include "quic/frame/if_frame.h"

namespace quicx {
namespace quic {

class MaxDataFrame:
    public IFrame {
public:
    MaxDataFrame();
    ~MaxDataFrame();

    virtual bool Encode(std::shared_ptr<common::IBuffer> buffer);
    virtual bool Decode(std::shared_ptr<common::IBuffer> buffer, bool with_type = false);
    virtual uint32_t EncodeSize();

    void SetMaximumData(uint64_t maximum) { maximum_data_ = maximum; }
    uint64_t GetMaximumData() { return maximum_data_; }

private:
    uint64_t maximum_data_;  // the maximum amount of data that can be sent on the entire connection.
};

}
}

#endif