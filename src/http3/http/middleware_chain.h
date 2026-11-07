#ifndef HTTP3_HTTP_MIDDLEWARE_CHAIN
#define HTTP3_HTTP_MIDDLEWARE_CHAIN

#include <functional>
#include <memory>
#include <vector>

#include "quicx/http3/type.h"

namespace quicx {
namespace http3 {

/**
 * @brief Method-aware middleware pipeline with short-circuit support (P1-H8)
 *
 * Replaces the old flat std::vector<http_handler> that ignored the method
 * filter and could not short-circuit. Registration order is preserved:
 * middlewares run in the order they were added (within before / after).
 *
 * Method filtering uses bitmask semantics: a middleware registered for
 * HttpMethod::kGet only runs for GET requests; kAny matches every
 * implemented method (kUnknown / extension methods are excluded, matching
 * the router's kAny fallback semantics).
 *
 * Short-circuit: a before-middleware returning false stops the remaining
 * before-middlewares, and signals the caller to skip the route handler and
 * after-middlewares. Return values of after-middlewares are ignored (there
 * is nothing left to skip at the tail of the pipeline).
 */
class MiddlewareChain {
public:
    /**
     * @brief Add a middleware to the chain
     * @param method The HTTP method filter (kAny matches all implemented methods)
     * @param position Run before or after the route handler
     * @param handler The middleware handler (false short-circuits before-pipelines)
     */
    void Add(HttpMethod method, MiddlewarePosition position, const middleware_handler& handler) {
        if (!handler) {
            return;
        }
        switch (position) {
            case MiddlewarePosition::kBefore:
                before_.emplace_back(Entry{method, handler});
                break;
            case MiddlewarePosition::kAfter:
                after_.emplace_back(Entry{method, handler});
                break;
            default:
                break;
        }
    }

    /**
     * @brief Run the before-pipeline for a request
     * @param request_method The HTTP method of the incoming request
     * @param request The HTTP request object
     * @param response The HTTP response object
     * @return true if the pipeline completed (or was empty); false if a
     *         middleware short-circuited — the response it wrote must be
     *         flushed and the route handler skipped
     */
    bool RunBefore(HttpMethod request_method,
                   std::shared_ptr<IRequest> request,
                   std::shared_ptr<IResponse> response) {
        return Run(before_, request_method, request, response);
    }

    /**
     * @brief Run the after-pipeline for a request
     *
     * Runs every matching middleware; short-circuit return values are
     * ignored (nothing left to skip at the tail).
     */
    void RunAfter(HttpMethod request_method,
                  std::shared_ptr<IRequest> request,
                  std::shared_ptr<IResponse> response) {
        // Deliberately NOT reusing Run(): a false return must not stop the
        // remaining after-middlewares — there is nothing left to skip at the
        // tail of the pipeline, so every matching middleware always runs.
        for (const Entry& entry : after_) {
            if (MethodMatches(entry.method, request_method)) {
                entry.handler(request, response);  // return value ignored
            }
        }
    }

    /**
     * @brief Whether a middleware registered for reg_method applies to a
     *        request carrying req_method
     *
     * kAny is the bitmask-OR of all implemented methods, so a single AND
     * covers both exact matches and the wildcard. kUnknown (0x200, outside
     * kAny) never matches, same as the router's kAny fallback tree.
     */
    static bool MethodMatches(HttpMethod reg_method, HttpMethod req_method) {
        return (static_cast<uint64_t>(reg_method) & static_cast<uint64_t>(req_method)) != 0;
    }

private:
    struct Entry {
        HttpMethod method;
        middleware_handler handler;
    };

    static bool Run(const std::vector<Entry>& chain,
                    HttpMethod request_method,
                    std::shared_ptr<IRequest> request,
                    std::shared_ptr<IResponse> response) {
        for (const Entry& entry : chain) {
            if (!MethodMatches(entry.method, request_method)) {
                continue;
            }
            if (!entry.handler(request, response)) {
                return false;
            }
        }
        return true;
    }

    std::vector<Entry> before_;
    std::vector<Entry> after_;
};

}  // namespace http3
}  // namespace quicx

#endif
