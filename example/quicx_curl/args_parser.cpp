#include <cstring>
#include <iostream>
#include "args_parser.h"

bool ArgsParser::Parse(int argc, char* argv[], CurlArgs& args) {
    if (argc < 2) {
        return false;
    }
    
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        
        if (arg[0] != '-') {
            // Positional argument (URL)
            if (args.url.empty()) {
                args.url = arg;
            }
            continue;
        }
        
        if (arg.size() >= 2 && arg[1] != '-') {
            // Short option
            std::string opt = arg.substr(1);
            std::string value = (i + 1 < argc) ? argv[i + 1] : "";
            if (!ParseShortOption(opt, value, i, argc, argv, args)) {
                return false;
            }
        } else if (arg.size() > 2 && arg[1] == '-') {
            // Long option
            std::string opt = arg.substr(2);
            std::string value;
            
            size_t eq_pos = opt.find('=');
            if (eq_pos != std::string::npos) {
                value = opt.substr(eq_pos + 1);
                opt = opt.substr(0, eq_pos);
            } else {
                value = (i + 1 < argc) ? argv[i + 1] : "";
            }
            
            if (!ParseLongOption(opt, value, i, argc, argv, args)) {
                return false;
            }
        }
    }
    
    return args.IsValid();
}

bool ArgsParser::ParseShortOption(const std::string& opt, const std::string& value,
                                  int& i, int argc, char* argv[], CurlArgs& args) {
    if (opt == "X") {
        args.method = GetNextArg(i, argc, argv);
    } else if (opt == "H") {
        args.headers.push_back(GetNextArg(i, argc, argv));
    } else if (opt == "d") {
        args.data = GetNextArg(i, argc, argv);
        if (args.method == "GET") {
            args.method = "POST";  // -d implies POST
        }
    } else if (opt == "o") {
        args.output_file = GetNextArg(i, argc, argv);
    } else if (opt == "i") {
        args.include_headers = true;
    } else if (opt == "v") {
        args.verbose = true;
    } else if (opt == "s") {
        args.silent = true;
    } else if (opt == "h") {
        args.show_help = true;
    } else {
        std::cerr << "Unknown option: -" << opt << std::endl;
        return false;
    }
    
    return true;
}

bool ArgsParser::ParseLongOption(const std::string& opt, const std::string& value,
                                 int& i, int argc, char* argv[], CurlArgs& args) {
    if (opt == "request") {
        args.method = GetNextArg(i, argc, argv);
    } else if (opt == "header") {
        args.headers.push_back(GetNextArg(i, argc, argv));
    } else if (opt == "data") {
        args.data = GetNextArg(i, argc, argv);
        if (args.method == "GET") {
            args.method = "POST";
        }
    } else if (opt == "output") {
        args.output_file = GetNextArg(i, argc, argv);
    } else if (opt == "include") {
        args.include_headers = true;
    } else if (opt == "verbose") {
        args.verbose = true;
    } else if (opt == "silent") {
        args.silent = true;
    } else if (opt == "http3-only") {
        args.http3_only = true;
    } else if (opt == "help") {
        args.show_help = true;
    } else {
        std::cerr << "Unknown option: --" << opt << std::endl;
        return false;
    }
    
    return true;
}

std::string ArgsParser::GetNextArg(int& i, int argc, char* argv[]) {
    if (i + 1 < argc) {
        return argv[++i];
    }
    return "";
}

void ArgsParser::ShowHelp(const char* program_name) {
    std::cout << "QuicX-Curl - HTTP/3 Command Line Tool\n\n";
    std::cout << "Usage: " << program_name << " [OPTIONS] <URL>\n\n";
    std::cout << "Basic Options:\n";
    std::cout << "  -X, --request <method>     HTTP method (GET, POST, PUT, DELETE, etc.)\n";
    std::cout << "  -H, --header <header>      Add HTTP header (can be used multiple times)\n";
    std::cout << "  -d, --data <data>          Request data (implies POST if not specified)\n";
    std::cout << "  -o, --output <file>        Write output to file instead of stdout\n";
    std::cout << "  -i, --include              Include response headers in output\n";
    std::cout << "  -v, --verbose              Verbose output (show request/response details)\n";
    std::cout << "  -s, --silent               Silent mode (no progress or error info)\n";
    std::cout << "  --http3-only               Force HTTP/3 only (default)\n";
    std::cout << "  -h, --help                 Show this help message\n\n";
    std::cout << "Examples:\n";
    std::cout << "  " << program_name << " https://example.com/api/users\n";
    std::cout << "  " << program_name << " -X POST -d '{\"name\":\"John\"}' https://example.com/api/users\n";
    std::cout << "  " << program_name << " -H \"Authorization: Bearer token\" https://example.com/api/data\n";
    std::cout << "  " << program_name << " -v -i https://example.com/api/status\n";
    std::cout << "  " << program_name << " -o output.json https://example.com/api/data\n\n";
}



