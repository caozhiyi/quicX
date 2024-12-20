#ifndef HTTP3_HTTP_ROUTER_IF_ROUTER
#define HTTP3_HTTP_ROUTER_IF_ROUTER

#include <string>
#include <unordered_map>
#include "http3/include/type.h"

namespace quicx {
namespace http3 {

/*
router manage all route context. there are three matching rules in router:
1. normal static path. like "/department/user/info", when user request "/department/user/info", it will match this route.
"/department/user" or "/department/user/info/1" will not match.
2. dynamic param path. like "/department/user/:id", when user request "/department/user/1", it will match this route.
3. wildcard path. like "/department/user/*", when user request "/department/user/info", it will match this route.
wildcard only locate at the end of path.

router prority:
1. static path > dynamic path > wildcard path, for example:
if add route "/department/user/info" and "/department/user/:id", when user request "/department/user/info", it will match "/department/user/info".
if add route "/department/user/:id" and "/department/user/*", when user request "/department/user/info", it will match "/department/user/:id".

2. try to match longest path, for example:
if add route "/department/user/info" and "/department/user/:info/:id", when user request "/department/user/info/1", it will match "/department/user/:info/:id".
if add route "/department/user/*" and "/department/user/info/:id", when user request "/department/user/info/1", it will match "/department/user/info/:id".

THINKING:
1. handler is associated with http, if it can implement as a templete type, it will be more flexible and easy to test, 
but in c++, templete class declaration and implementation must be in the same file, so it will encounter circular reference problem 
when dynamic create subclass instance in parents class. 
*/

struct MatchResult {
    bool is_match;
    http_handler handler;                                // http handler
    std::unordered_map<std::string, std::string> params; // request params
};

class IRouter {
public:
    IRouter() {}
    virtual ~IRouter() {}

    // add route context
    virtual bool AddRoute(HttpMothed mothed, const std::string& path, const http_handler& handler) = 0;

    // router match
    virtual MatchResult Match(HttpMothed mothed, const std::string& path) = 0;
};

}
}

#endif
