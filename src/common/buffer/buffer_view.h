// Use of this source code is governed by a BSD 3-Clause License
// that can be found in the LICENSE file.

// Author: caozhiyi (caozhiyi5@gmail.com)

#ifndef COMMON_BUFFER_BUFFER_VIEW
#define COMMON_BUFFER_BUFFER_VIEW

#include <cstdint>

namespace quicx {

class BufferView {
public:
    BufferView();
    BufferView(const BufferView& view);
    ~BufferView();

    void Clear();
    bool IsEmpty();

    void SetData(uint8_t* data, uint32_t len);

    uint8_t* GetData();
    uint32_t GetLength();

    BufferView& operator=(const BufferView& view);

private:
    uint8_t* _view_data;
    uint32_t _view_length;
};

}

#endif