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

    void SetData(char* data, uint32_t len);

    char* GetData();
    uint32_t GetLength();

    BufferView& operator=(const BufferView& view);

private:
    char* _view_data;
    uint32_t _view_length;

};

}

#endif