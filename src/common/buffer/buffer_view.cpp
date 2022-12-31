// Use of this source code is governed by a BSD 3-Clause License
// that can be found in the LICENSE file.

// Author: caozhiyi (caozhiyi5@gmail.com)

#include "common/buffer/buffer_view.h"

namespace quicx {

BufferView::BufferView():
   _view_data(nullptr),
   _view_length(0) {

}

BufferView::BufferView(const BufferView& view):
    _view_data(view._view_data),
    _view_length(view._view_length) {

}

BufferView::BufferView(const uint8_t* data, uint32_t length) {
    SetData(data, length);
}

BufferView::~BufferView() {

}

void BufferView::Clear() {
    _view_data = nullptr;
    _view_length = 0;
}

bool BufferView::IsEmpty() {
    return _view_data == nullptr;
}

void BufferView::SetData(const uint8_t* data, uint32_t len) {
    _view_data = data;
    _view_length = len;

}

const uint8_t* BufferView::GetData() {
    return _view_data;
}

uint32_t BufferView::GetLength() {
    return _view_length;
}

BufferView& BufferView::operator=(const BufferView& view) {
    _view_data = view._view_data;
    _view_length = view._view_length;
    return *this;
}

}
