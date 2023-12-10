#include "http3/qpack/static_table.h"

namespace quicx {
namespace http3 {

std::vector<HeaderItem> StaticTable::_headeritem_vec = {
    // 0
    {":authority",                        ""},
    {":path",                             "/"},
    {"age",                               "0"},
    {"content-disposition",               ""},
    {"content-length",                    "0"},
    {"cookie",                            ""},
    {"date",                              ""},
    {"etag",                              ""},
    // 8
    {"if-modified-since",                 ""},
    {"if-none-match",                     ""},
    {"last-modified",                     ""},
    {"link",                              ""},
    {"location",                          ""},
    {"referer",                           ""},
    {"set-cookie",                        ""},
    {":method",                           "CONNECT"},
    // 16
    {":method",                           "DELETE"},
    {":method",                           "GET"},
    {":method",                           "HEAD"},
    {":method",                           "OPTIONS"},
    {":method",                           "POST"},
    {":method",                           "PUT"},
    {":scheme",                           "http"},
    {":scheme",                           "https"},
    // 24
    {":status",                           "103"},
    {":status",                           "200"},
    {":status",                           "304"},
    {":status",                           "404"},
    {":status",                           "503"},
    {"accept",                            "*/*"},
    {"accept",                            "application/dns-message"},
    {"accept-encoding",                   "gzip, deflate, br"},
    // 32
    {"accept-ranges",                     "bytes"},
    {"access-control-allow-headers",      "cache-control"},
    {"access-control-allow-headers",      "content-type"},
    {"access-control-allow-origin",       "*"},
    {"cache-control",                     "max-age=0"},
    {"cache-control",                     "max-age=2592000"},
    {"cache-control",                     "max-age=604800"},
    {"cache-control",                     "no-cache"},
    // 40
    {"cache-control",                     "no-store"},
    {"cache-control",                     "public, max-age=31536000"},
    {"content-encoding",                  "br"},
    {"content-encoding",                  "gzip"},
    {"content-type",                      "application/dns-message"},
    {"content-type",                      "application/javascript"},
    {"content-type",                      "application/json"},
    {"content-type",                      "application/x-www-form-urlencoded"},
    // 48
    {"content-type",                      "image/gif"},
    {"content-type",                      "image/jpeg"},
    {"content-type",                      "image/png"},
    {"content-type",                      "text/css"},
    {"content-type",                      "text/html;charset=utf-8"},
    {"content-type",                      "text/plain"},
    {"content-type",                      "text/plain;charset=utf-8"},
    {"range",                             "bytes=0-"},
    // 56
    {"strict-transport-security",         "max-age=31536000"},
    {"strict-transport-security",         "max-age=31536000;includesubdomains"},
    {"strict-transport-security",         "max-age=31536000;includesubdomains;preload"},
    {"vary",                              "accept-encoding"},
    {"vary",                              "origin"},
    {"x-content-type-options",            "nosniff"},
    {"x-xss-protection",                  "1;mode=block"},
    {":status",                           "100"},
    // 64
    {":status",                           "204"},
    {":status",                           "206"},
    {":status",                           "302"},
    {":status",                           "400"},
    {":status",                           "403"},
    {":status",                           "421"},
    {":status",                           "425"},
    {":status",                           "500"},
    // 72
    {"accept-language",                   ""},
    {"access-control-allow-credentials",  "FALSE"},
    {"access-control-allow-credentials",  "TRUE"},
    {"access-control-allow-headers",      "*"},
    {"access-control-allow-methods",      "get"},
    {"access-control-allow-methods",      "get, post, options"},
    {"access-control-allow-methods",      "options"},
    {"access-control-expose-headers",     "content-length"},
    // 80
    {"access-control-request-headers",    "content-type"},
    {"access-control-request-method",     "get"},
    {"access-control-request-method",     "post"},
    {"alt-svc",                           "clear"},
    {"authorization",                     ""},
    {"content-security-policy",           "script-src 'none';object-src 'none';base-uri 'none'"},
    {"early-data",                        "1"},
    {"expect-ct",                         ""},
    // 88
    {"forwarded",                         ""},
    {"if-range",                          ""},
    {"origin",                            ""},
    {"purpose",                           "prefetch"},
    {"server",                            ""},
    {"timing-allow-origin",               "*"},
    {"upgrade-insecure-requests",         "1"},
    {"user-agent",                        ""},
    // 96
    {"x-forwarded-for",                   ""},
    {"x-frame-options",                   "deny"},
    {"x-frame-options",                   "sameorigin"}
};

std::unordered_map<std::string, uint32_t> StaticTable::_headeritem_index_map;

StaticTable::StaticTable() {
    static bool init = false;
    if (init) {
        return;
    }
    init = true;
    for (uint32_t i = 0; i < _headeritem_vec.size(); i++) {
        const HeaderItem& iter = _headeritem_vec[i];
        _headeritem_index_map[iter._name + iter._value] = i;
    }
}

HeaderItem* StaticTable::GetHeaderItem(uint32_t index) {
    if (index < _headeritem_vec.size()) {
        return &_headeritem_vec[index];
    }
    return nullptr;
}

int32_t StaticTable::GetHeaderItem(const std::string& name, const std::string& value) {
    auto iter = _headeritem_index_map.find(name + value);
    if (iter != _headeritem_index_map.end()) {
        return iter->second;
    }
    return -1;
}

}
}