#ifndef HTTP3_FRAME_DATA_FRAME
#define HTTP3_FRAME_DATA_FRAME

#include "http3/frame/type.h"
#include "common/buffer/buffer.h"
#include "http3/frame/frame_interface.h"

namespace quicx {
namespace http3 {

class DataFrame:
    public IFrame {
public:
    DataFrame(): IFrame(FT_DATA) {}
    virtual ~DataFrame() {}

    uint32_t GetLength() { return _length; }

protected:
    uint32_t _length;
};

}
}

#endif
