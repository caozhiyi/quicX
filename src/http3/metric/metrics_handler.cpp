#include "http3/metric/metrics_handler.h"
#include "common/log/log.h"
#include "common/metrics/metrics.h"

namespace quicx {
namespace http3 {

void MetricsHandler::Handle(std::shared_ptr<IRequest> request, std::shared_ptr<IResponse> response) {
    // Export metrics in Prometheus format
    std::string metrics_data = common::Metrics::ExportPrometheus();

    // Set response headers
    response->SetStatusCode(200);
    response->AddHeader("Content-Type", "text/plain; version=0.0.4; charset=utf-8");

    // Set response body
    response->AppendBody(metrics_data);

    common::LOG_DEBUG("Metrics endpoint accessed, exported %zu bytes", metrics_data.size());
}

}  // namespace http3
}  // namespace quicx
