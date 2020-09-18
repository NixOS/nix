#pragma once

#include "logging.hh"

namespace nix {

enum struct LogFormat {
    raw,
    rawWithLogs,
    internalJson,
    bar,
    barWithLogs,
};

struct LoggerBuilder {
    std::string name;
    std::function<Logger *()> builder;
};

extern std::set<std::string> logFormats;

extern std::vector<std::shared_ptr<LoggerBuilder>> registeredLoggers;

void registerLogger(std::string name, std::function<Logger *()> builder);

}
