#ifndef TOOL_QC_SESSION_TRANSPORT_MAP_H
#define TOOL_QC_SESSION_TRANSPORT_MAP_H

#include <quicx/http3/if_client.h>

#include "cli/options.h"

// Pure functions translating CLI options into library config structs.
// All switch-style H3 knobs (qlog/keylog/0rtt/version/ecn/key-update/
// keep-alive/session-cache) are mapped HERE and nowhere else, so library
// interface drift touches a single file.

quicx::Http3ClientConfig BuildClientConfig(const TransportOptions& tp, double connect_timeout_s,
    bool verbose);
quicx::Http3Settings BuildHttp3Settings(const TransportOptions& tp);

#endif  // TOOL_QC_SESSION_TRANSPORT_MAP_H
