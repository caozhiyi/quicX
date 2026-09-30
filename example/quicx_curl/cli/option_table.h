#ifndef TOOL_QC_CLI_OPTION_TABLE_H
#define TOOL_QC_CLI_OPTION_TABLE_H

#include <functional>
#include <string>
#include <vector>

#include "options.h"

// ============================================================================
// Data-driven option registry. Adding an option = adding one row in
// option_table.cpp; parsing/help generation stay generic.
// ============================================================================

struct OptionSpec {
    char short_name = 0;          // 0 = no short form
    const char* long_name = nullptr;  // nullptr = no long form
    const char* group = "";       // help section: http/transport/h3/output/common
    bool takes_value = false;
    const char* arg_name = "";    // shown in help, e.g. "<file>"
    const char* help = "";
    // Returns false on invalid value (error message printed by caller).
    std::function<bool(const std::string& value, Options& opts)> apply;
};

class OptionTable {
public:
    OptionTable();

    // Parses argv. Returns false and fills `error` on bad usage.
    // Remaining (non-option) arguments are collected as urls.
    bool Parse(int argc, char* argv[], Options& opts, std::string& error) const;

    void PrintHelp(const char* program, std::FILE* out) const;

private:
    const OptionSpec* FindShort(char c) const;
    const OptionSpec* FindLong(const std::string& name) const;
    void PrintGroup(const char* group, std::FILE* out) const;

    std::vector<OptionSpec> specs_;
};

#endif  // TOOL_QC_CLI_OPTION_TABLE_H
