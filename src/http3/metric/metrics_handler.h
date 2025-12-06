#ifndef HTTP3_METRIC_METRICS_HANDLER_H
#define HTTP3_METRIC_METRICS_HANDLER_H

#include "http3/include/if_request.h"
#include "http3/include/if_response.h"

namespace quicx {
namespace http3 {

/**
 * @brief Metrics handler for Prometheus-format metrics export
 *
 * This handler provides a built-in endpoint that exports metrics
 * in Prometheus text format via HTTP/3.
 */
class MetricsHandler {
public:
    /**
     * @brief Handle metrics request
     *
     * Exports all registered metrics in Prometheus text format.
     *
     * @param request HTTP request (unused, but required by handler signature)
     * @param response HTTP response to populate with metrics data
     */
    static void Handle(std::shared_ptr<IRequest> request, std::shared_ptr<IResponse> response);
};

}  // namespace http3
}  // namespace quicx

#endif  // HTTP3_METRIC_METRICS_HANDLER_H
