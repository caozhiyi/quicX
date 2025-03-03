#ifndef UPGRADE_UPGRADE_TYPE
#define UPGRADE_UPGRADE_TYPE

#include <functional>
#include <unordered_map>
#include "upgrade/upgrade/if_socket_handler.h"

namespace quicx {
namespace upgrade {

enum class HttpVersion {
    kHttp1,
    kHttp2,
};

}
}

#endif