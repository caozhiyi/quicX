#ifndef HTTP3_HTTP_CONFIG
#define HTTP3_HTTP_CONFIG

namespace quicx {
namespace http3 {

uint32_t max_headers_list_size__ = 100;
uint32_t qpack_max_table_capacity__ = 4096;
uint32_t qpack_max_blocked_streams__ = 10;
uint32_t enable_push__ = 0;

}
}

#endif