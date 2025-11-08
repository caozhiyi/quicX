#ifndef HTTP3_ROUTER_IF_ROUTER
#define HTTP3_ROUTER_IF_ROUTER

#include <string>
#include <variant>
#include <unordered_map>
#include "http3/include/type.h"
#include "http3/include/if_async_handler.h"

namespace quicx {
namespace http3 {

/**
 * @brief Route configuration data
 * 
 * Router stores this configuration but doesn't interpret the semantic meaning.
 * It's protocol-agnostic and just responsible for storing and matching routes.
 * 
 * This design allows router to remain independent of HTTP-specific processing logic.
 * 
 * The handler variant can hold either:
 * - http_handler: for complete mode (entire body buffered)
 * - std::shared_ptr<IAsyncServerHandler>: for streaming mode (async callbacks)
 */
struct RouteConfig {
    std::variant<http_handler, std::shared_ptr<IAsyncServerHandler>, std::shared_ptr<IAsyncClientHandler>> handler;
    
    // Default constructor: empty handler
    RouteConfig() : handler(http_handler{nullptr}) {}
    
    // Constructor for complete mode handler
    explicit RouteConfig(const http_handler& h) : handler(h) {}
    
    // Constructor for async mode handler
    explicit RouteConfig(std::shared_ptr<IAsyncServerHandler> h) : handler(h) {}
    
    // Constructor for async client handler
    explicit RouteConfig(std::shared_ptr<IAsyncClientHandler> h) : handler(h) {}
    
    /**
     * @brief Check if this route uses async handler
     * @return true if handler is IAsyncServerHandler, false if http_handler
     */
    bool IsAsync() const { 
        return IsAsyncClient() || IsAsyncServer();
    }
    
    // Check if this route uses async client handler
    bool IsAsyncClient() const {
        return std::holds_alternative<std::shared_ptr<IAsyncClientHandler>>(handler);
    }

    // Check if this route uses async server handler
    bool IsAsyncServer() const {
        return std::holds_alternative<std::shared_ptr<IAsyncServerHandler>>(handler);
    }

    /**
     * @brief Get the complete handler
     * @return http_handler if this is complete mode, nullptr otherwise
     */
    http_handler GetCompleteHandler() const {
        if (std::holds_alternative<http_handler>(handler)) {
            return std::get<http_handler>(handler);
        }
        return nullptr;
    }

    /**
     * @brief Get the async client handler
     * @return IAsyncClientHandler if this is async client mode, nullptr otherwise
     */
    std::shared_ptr<IAsyncClientHandler> GetAsyncClientHandler() const {
        if (std::holds_alternative<std::shared_ptr<IAsyncClientHandler>>(handler)) {
            return std::get<std::shared_ptr<IAsyncClientHandler>>(handler);
        }
        return nullptr;
    }

    /**
     * @brief Get the async server handler
     * @return IAsyncServerHandler if this is async server mode, nullptr otherwise
     */
    std::shared_ptr<IAsyncServerHandler> GetAsyncServerHandler() const {
        if (std::holds_alternative<std::shared_ptr<IAsyncServerHandler>>(handler)) {
            return std::get<std::shared_ptr<IAsyncServerHandler>>(handler);
        }
        return nullptr;
    }
};

/**
 * @brief Result of router matching
 */
struct MatchResult {
    bool is_match = false;                               // Whether a route was matched
    RouteConfig config;                                  // Route configuration (handler + mode)
    std::unordered_map<std::string, std::string> params; // Path parameters extracted from URL
};

/**
 * @brief Router interface for HTTP request routing
 * 
 * @note
 * router manage all route context. there are three matching rules in router:
 * 1. normal static path. like "/department/user/info", when user request "/department/user/info", it will match this route.
 * "/department/user" or "/department/user/info/1" will not match.
 * 2. dynamic param path. like "/department/user/:id", when user request "/department/user/1", it will match this route.
 * 3. wildcard path. like "/department/user/\*", when user request "/department/user/info", it will match this route.
 * wildcard only locate at the end of path.
 * 
 * router prority:
 * 1. static path > dynamic path > wildcard path, for example:
 * if add route "/department/user/info" and "/department/user/:id", when user request "/department/user/info", it will match "/department/user/info".
 * if add route "/department/user/:id" and "/department/user/\*", when user request "/department/user/info", it will match "/department/user/:id".
 * 
 * 2. try to match longest path, for example:
 * if add route "/department/user/info" and "/department/user/:info/:id", when user request "/department/user/info/1", it will match "/department/user/:info/:id".
 * if add route "/department/user/\*" and "/department/user/info/:id", when user request "/department/user/info/1", it will match "/department/user/info/:id".
 * 
 * THINKING:
 * 1. handler is associated with http, if it can implement as a templete type, it will be more flexible and easy to test, 
 * but in c++, templete class declaration and implementation must be in the same file, so it will encounter circular reference problem 
 * when dynamic create subclass instance in parents class. 
 */
class IRouter {
public:
    IRouter() {}
    virtual ~IRouter() {}

    /**
     * @brief Add a route with configuration
     * @param method HTTP method
     * @param path URL path pattern (supports ":param" and "*" wildcards)
     * @param config Route configuration containing handler and metadata
     * @return true if route was added successfully
     */
    virtual bool AddRoute(HttpMethod method, const std::string& path, 
                         const RouteConfig& config) = 0;

    /**
     * @brief Match a route based on method and path
     * @param method HTTP method
     * @param path URL path
     * @return Match result containing route config and extracted parameters
     */
    virtual MatchResult Match(HttpMethod method, const std::string& path) = 0;
};

}
}

#endif
