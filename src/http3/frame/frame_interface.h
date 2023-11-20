#ifndef HTTP3_FRAME_FRAME_INTERFACE
#define HTTP3_FRAME_FRAME_INTERFACE

#include <cstdint>
#include "http3/frame/type.h"

namespace http3 {

class IFrame {
public:
    IFrame(uint16_t ft = FT_UNKNOW): _frame_type(ft) {}
    virtual ~IFrame() {}

    uint16_t GetType() { return _frame_type; }

protected:
    uint16_t _frame_type;
};

}


#endif
