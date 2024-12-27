#include "http3/router/util.h"

namespace quicx {
namespace http3 {

std::string PathParse(const std::string& path, int& offset) {
    int32_t sub_str_start = -1;
    int32_t sub_str_end = -1;
    bool done = false;

    for (size_t i = offset; i < path.size() && !done; i++) {
        switch (path[i]) {
        case '/':
            if (sub_str_start < 0) {
                sub_str_start = i;
                sub_str_end = i + 1; // only one slash last, like "/abc/"

            } else {
                sub_str_end = i; // offset doesn't include the last slash
                done = true;
            }
            break;

        default:
            sub_str_end = i + 1; // offset is over the last char
            break;
        }
    }
    if (sub_str_start < 0 || sub_str_end < 0) {
        return "";
    }
    
    offset = sub_str_end;
    return std::move(path.substr(sub_str_start, sub_str_end - sub_str_start));
}

}
}

