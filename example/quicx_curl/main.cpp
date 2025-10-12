#include <iostream>
#include <csignal>
#include <cstdlib>
#include "args_parser.h"
#include "http_client.h"
#include "output_formatter.h"

static volatile std::sig_atomic_t g_interrupted = 0;

static void SignalHandler(int signal) {
    g_interrupted = 1;
    std::cerr << "\nInterrupted by user" << std::endl;
    exit(1);
}

int main(int argc, char* argv[]) {
    // Install signal handler
    std::signal(SIGINT, SignalHandler);
    std::signal(SIGTERM, SignalHandler);
    
    // Parse arguments
    ArgsParser parser;
    CurlArgs args;
    
    if (!parser.Parse(argc, argv, args)) {
        if (!args.show_help) {
            std::cerr << "Error: Invalid arguments" << std::endl;
            std::cerr << "Try '" << argv[0] << " --help' for more information." << std::endl;
            return 1;
        }
    }
    
    if (args.show_help) {
        ArgsParser::ShowHelp(argv[0]);
        return 0;
    }
    
    if (!args.IsValid()) {
        std::cerr << "Error: URL is required" << std::endl;
        std::cerr << "Try '" << argv[0] << " --help' for more information." << std::endl;
        return 1;
    }
    
    // Determine output mode
    OutputMode output_mode = OutputMode::kNormal;
    if (args.silent) {
        output_mode = OutputMode::kSilent;
    } else if (args.verbose) {
        output_mode = OutputMode::kVerbose;
    } else if (args.include_headers) {
        output_mode = OutputMode::kInclude;
    }
    
    if (args.verbose) {
        std::cerr << "* URL: " << args.url << std::endl;
        std::cerr << "* Method: " << args.method << std::endl;
        if (!args.data.empty()) {
            std::cerr << "* Data: " << args.data << std::endl;
        }
    }
    
    // Initialize HTTP client
    HttpClient client;
    if (!client.Init(args.verbose)) {
        std::cerr << "Failed to initialize HTTP client" << std::endl;
        return 1;
    }
    
    // Execute request
    HttpResponse response;
    bool success = client.DoRequest(
        args.url,
        args.method,
        args.headers,
        args.data,
        response
    );
    
    if (!success) {
        if (!args.silent) {
            std::cerr << "Request failed";
            if (response.error != 0) {
                std::cerr << " with error code: " << response.error;
            }
            std::cerr << std::endl;
        }
        return 1;
    }
    
    // Format and output response
    OutputFormatter formatter(output_mode);
    
    if (!args.output_file.empty()) {
        // Write to file
        if (!formatter.WriteToFile(response, args.output_file)) {
            return 1;
        }
        
        if (args.verbose) {
            std::cerr << "* Saved to " << args.output_file << std::endl;
        }
    } else {
        // Write to stdout
        formatter.WriteResponse(response, std::cout);
    }
    
    // Exit with appropriate code
    if (response.status_code >= 400) {
        return 22;  // curl's HTTP error exit code
    }
    
    return 0;
}

