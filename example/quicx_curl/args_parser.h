#ifndef TOOL_QUICX_CURL_ARGS_PARSER
#define TOOL_QUICX_CURL_ARGS_PARSER

#include <string>
#include <vector>

struct CurlArgs {
    std::string url;
    std::string method = "GET";
    std::vector<std::string> headers;
    std::string data;
    std::string output_file;
    bool include_headers = false;
    bool verbose = false;
    bool silent = false;
    bool show_help = false;
    bool http3_only = true;  // Default to HTTP/3
    
    // Validation
    bool IsValid() const {
        return !url.empty();
    }
};

class ArgsParser {
public:
    ArgsParser() = default;
    
    // Parse command line arguments
    bool Parse(int argc, char* argv[], CurlArgs& args);
    
    // Show usage/help information
    static void ShowHelp(const char* program_name);
    
private:
    bool ParseShortOption(const std::string& opt, const std::string& value, 
                         int& i, int argc, char* argv[], CurlArgs& args);
    bool ParseLongOption(const std::string& opt, const std::string& value,
                        int& i, int argc, char* argv[], CurlArgs& args);
    std::string GetNextArg(int& i, int argc, char* argv[]);
};

#endif

