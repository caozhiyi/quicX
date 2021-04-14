#ifndef QUIC_CONNECTION_CONNECTION_INTERFACE
#define QUIC_CONNECTION_CONNECTION_INTERFACE

#include <string>

namespace quicx {

// parsing versions from both GoogleQUIC and IETF QUIC
// see https://docs.google.com/document/d/1GV2j-PGl7YGFqmWbYvzu7-UNVIpFdbprtmN9tt6USG8/preview# 
std::string ParseVersion(char v);

}

#endif