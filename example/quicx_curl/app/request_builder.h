#ifndef TOOL_QC_APP_REQUEST_BUILDER_H
#define TOOL_QC_APP_REQUEST_BUILDER_H

#include <memory>
#include <string>

#include <quicx/http3/type.h>

#include "cli/options.h"

class CookieJar;

// Translates Options into an IRequest for one URL. Applies in curl order:
// default UA, -H headers, -A/-e/-u, cookies from jar, then body (-d/-T).
// Returns nullptr on unreadable body file.
std::shared_ptr<quicx::IRequest> BuildRequest(const RequestSpec& spec, const CookieJar* jar,
    const std::string& url);

// "GET" -> quicx::HttpMethod; unknown maps to kGet (mirrors old behavior).
quicx::HttpMethod MethodFromString(const std::string& method);
std::string MethodToString(quicx::HttpMethod method);

#endif  // TOOL_QC_APP_REQUEST_BUILDER_H
