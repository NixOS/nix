#pragma once

#include "logging.hh"

namespace nix {

enum struct LogFormat {
    raw,
    rawWithLogs,
    internalJSON,
    bar,
    barWithLogs,
};

struct LoggerBuilder {
    std::string name;
    std::function<Logger *()> builder;
};

void registerLogger(std::string name, std::function<Logger *()> builder);

std::vector<std::shared_ptr<LoggerBuilder>> getRegisteredLoggers();

}
