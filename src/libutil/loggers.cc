#include "loggers.hh"

namespace nix {

std::set<std::string> logFormats = {
    "raw",
    "raw-with-logs",
    "internal-json",
    "bar",
    "bar-with-logs"
};

std::vector<std::shared_ptr<LoggerBuilder>> registeredLoggers;

void registerLogger(std::string name, std::function<Logger *()> builder)
{
    LoggerBuilder lBuilder { .name = name, .builder = builder };
    registeredLoggers.push_back(std::make_shared<LoggerBuilder>(lBuilder));
}

}
