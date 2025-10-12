#include <fstream>
#include <iostream>

#include "output_formatter.h"

OutputFormatter::OutputFormatter(OutputMode mode) : mode_(mode) {
}

void OutputFormatter::WriteResponse(const HttpResponse& response, std::ostream& os) {
    if (mode_ == OutputMode::kSilent) {
        // Silent mode: only body to stdout, errors to stderr
        if (response.error == 0) {
            WriteBody(response, os);
        }
        return;
    }
    
    if (mode_ == OutputMode::kVerbose || mode_ == OutputMode::kInclude) {
        WriteHeaders(response, os);
        os << "\n";
    }
    
    WriteBody(response, os);
    
    if (mode_ == OutputMode::kVerbose) {
        WriteStats(response, std::cerr);
    }
}

bool OutputFormatter::WriteToFile(const HttpResponse& response, const std::string& filename) {
    std::ofstream file(filename, std::ios::binary);
    if (!file) {
        std::cerr << "Failed to open output file: " << filename << std::endl;
        return false;
    }
    
    WriteBody(response, file);
    file.close();
    
    if (mode_ == OutputMode::kVerbose) {
        std::cerr << "* Saved " << response.body.size() << " bytes to " << filename << std::endl;
    }
    
    return true;
}

void OutputFormatter::WriteHeaders(const HttpResponse& response, std::ostream& os) {
    // Status line
    os << "HTTP/3 " << response.status_code;
    
    // Status text
    if (response.status_code == 200) os << " OK";
    else if (response.status_code == 201) os << " Created";
    else if (response.status_code == 204) os << " No Content";
    else if (response.status_code == 400) os << " Bad Request";
    else if (response.status_code == 401) os << " Unauthorized";
    else if (response.status_code == 403) os << " Forbidden";
    else if (response.status_code == 404) os << " Not Found";
    else if (response.status_code == 500) os << " Internal Server Error";
    
    os << "\n";
    
    // Headers
    for (const auto& header : response.headers) {
        os << header.first << ": " << header.second << "\n";
    }
}

void OutputFormatter::WriteBody(const HttpResponse& response, std::ostream& os) {
    os << response.body;
    if (!response.body.empty() && response.body.back() != '\n') {
        os << "\n";
    }
}

void OutputFormatter::WriteStats(const HttpResponse& response, std::ostream& os) {
    os << "\n";
    os << "* Connection: HTTP/3\n";
    os << "* Status: " << response.status_code << "\n";
    os << "* Time: " << response.GetDurationMs() << " ms\n";
    os << "* Size: " << response.body.size() << " bytes\n";
    
    if (response.error != 0) {
        os << "* Error: " << response.error << "\n";
    }
}
