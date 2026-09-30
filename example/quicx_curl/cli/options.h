#ifndef TOOL_QC_CLI_OPTIONS_H
#define TOOL_QC_CLI_OPTIONS_H

#include <cstdint>
#include <string>
#include <vector>

// ============================================================================
// Three-section Options. The parser only fills this struct; business modules
// only read from it. No logic lives here.
// ============================================================================

struct RequestSpec {
    std::vector<std::string> urls;
    std::string method = "GET";        // -X
    std::vector<std::string> headers;  // -H "Name: value" (raw, may repeat)
    std::string data;                  // -d (inline body)
    std::string data_file;             // -d @file
    std::string upload_file;           // -T file (streamed via body provider)
    bool head = false;                 // -I
    std::string user_agent;            // -A
    std::string referer;               // -e
    std::string user;                  // -u user:password
    bool follow_redirect = false;      // -L
    int max_redirects = 50;            // --max-redirs
    std::string cookie_read_file;      // -b (file or inline cookie string)
    std::string cookie_write_file;     // -c (Netscape jar write-back)
    double connect_timeout_s = 0;      // --connect-timeout (0 = library default)
    double max_time_s = 0;             // --max-time (0 = unlimited)
};

struct TransportOptions {
    bool insecure = false;             // -k
    std::string cacert;                // --cacert
    std::string quic_version = "v1";   // --quic-version v1|v2
    bool zero_rtt = false;             // --0rtt (implies session cache)
    std::string session_cache;         // --session-cache <dir>
    std::string qlog_dir;              // --qlog <dir>
    std::string keylog_file;           // --keylog <file>
    bool ecn = false;                  // --ecn
    bool key_update = false;           // --key-update
    uint32_t keep_alive_ms = 0;        // --keep-alive <ms> (0 = off)
    bool migrate = false;              // --migrate
    std::string migrate_to;            // --migrate-to <ip[:port]> (empty = same ip, new port)
    uint32_t migrate_delay_ms = 0;     // --migrate-delay <ms> (0 = after request)
    bool push = false;                 // --push
};

struct OutputOptions {
    bool verbose = false;              // -v
    bool silent = false;               // -s
    bool show_error = true;            // -S (effective with -s)
    bool include_headers = false;      // -i
    bool remote_name = false;          // -O
    std::vector<std::string> output_files;  // -o (matched to urls by index)
    std::string write_out;             // -w
    bool fail = false;                 // -f
    bool stats = false;                // --stats
    bool progress_bar = false;         // -#
};

struct Options {
    RequestSpec req;
    TransportOptions tp;
    OutputOptions out;
    bool show_help = false;

    bool IsValid() const { return !req.urls.empty(); }
};

#endif  // TOOL_QC_CLI_OPTIONS_H
