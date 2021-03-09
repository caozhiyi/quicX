#ifndef COMMON_BUFFER_BUFFER_INTERFACE
#define COMMON_BUFFER_BUFFER_INTERFACE

#include <memory>
#include "alloter/alloter_interface.h"

namespace quicx {

class Buffer {
public:
    Buffer() {}
    virtual ~Buffer() {}

    // read to res buf but don't chenge the read point
    // return read size
    virtual uint32_t ReadNotClear(char* res, uint32_t len) = 0;

    virtual uint32_t Read(char* res, uint32_t len) = 0;
    virtual uint32_t Write(const char* str, uint32_t len) = 0;

    // clear all if len = 0
    // or modify read point
    virtual uint32_t Clear(uint32_t len = 0) = 0;
    
    // move write point
    virtual uint32_t MoveWritePt(uint32_t len) = 0;

    // do not read when buffer less than len. 
    // return len when read otherwise return 0
    virtual uint32_t ReadUntil(char* res, uint32_t len) = 0;
    
    // do not read when can't find specified character.
    // return read bytes when read otherwise return 0
    // when find specified character but res'length is too short, 
    // return 0 and the last param return need length
    virtual uint32_t ReadUntil(char* res, uint32_t len, const char* find, uint32_t find_len, uint32_t& need_len) = 0;
    
    virtual uint32_t GetFreeLength() = 0;
    virtual uint32_t GetCanReadLength() = 0;

    // return can read bytes
    virtual uint32_t FindStr(const char* s, uint32_t s_len) = 0;
};
}

#endif