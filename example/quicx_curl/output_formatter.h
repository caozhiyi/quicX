#ifndef TOOL_QUICX_CURL_OUTPUT_FORMATTER
#define TOOL_QUICX_CURL_OUTPUT_FORMATTER

#include <string>
#include <ostream>
#include "http_client.h"

enum class OutputMode {
    kNormal,    // Just response body
    kInclude,   // Include response headers
    kVerbose,   // Verbose debug info
    kSilent     // No output except body
};

class OutputFormatter {
public:
    OutputFormatter(OutputMode mode = OutputMode::kNormal);
    
    // Format and write response to output stream
    void WriteResponse(const HttpResponse& response, std::ostream& os);
    
    // Write to file
    bool WriteToFile(const HttpResponse& response, const std::string& filename);
    
private:
    OutputMode mode_;
    
    void WriteHeaders(const HttpResponse& response, std::ostream& os);
    void WriteBody(const HttpResponse& response, std::ostream& os);
    void WriteStats(const HttpResponse& response, std::ostream& os);
};

#endif

