#include "loggers.hh"

namespace nix {

static std::vector<std::shared_ptr<LoggerBuilder>>* registeredLoggers;

void initRegisteredLoggers()
{
    if (!registeredLoggers)
        registeredLoggers = new std::vector<std::shared_ptr<LoggerBuilder>>();
}

void registerLogger(std::string name, std::function<Logger *()> builder)
{
    LoggerBuilder lBuilder { .name = name, .builder = builder };
    initRegisteredLoggers();
    registeredLoggers->push_back(std::make_shared<LoggerBuilder>(lBuilder));
}

std::vector<std::shared_ptr<LoggerBuilder>> getRegisteredLoggers()
{
    return *registeredLoggers;
}

}
