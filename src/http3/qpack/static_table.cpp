#include "http3/qpack/static_table.h"

namespace quicx {
namespace http3 {

StaticTable::StaticTable() {
    headeritem_vec_ = {
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
    for (uint32_t i = 0; i < headeritem_vec_.size(); i++) {
        const HeaderItem& iter = headeritem_vec_[i];
        headeritem_index_map_[{iter.name_, iter.value_}] = i;
        headeritem_name_map_[iter.name_] = i;
    }
}

HeaderItem* StaticTable::FindHeaderItem(uint32_t index) {
    if (index < headeritem_vec_.size()) {
        return &headeritem_vec_[index];
    }
    return nullptr;
}

int32_t StaticTable::FindHeaderItemIndex(const std::string& name, const std::string& value) {
    auto iter = headeritem_index_map_.find({name, value});
    if (iter != headeritem_index_map_.end()) {
        return iter->second;
    }
    return -1;
}

int32_t StaticTable::FindHeaderItemIndex(const std::string& name) {
    auto iter = headeritem_name_map_.find(name);
    if (iter != headeritem_name_map_.end()) {
        return iter->second;
    }
    return -1;
}

}
}