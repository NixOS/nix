#pragma once
///@file

#include "logging.hh"
#include "types.hh"

namespace nix {

enum class LogFormat {
  raw,
  rawWithLogs,
  internalJSON,
  bar,
  barWithLogs,
};

void setLogFormat(const std::string & logFormatStr);
void setLogFormat(const LogFormat & logFormat);

void createDefaultLogger();

class RunPager{
private:
    Logger* previousLogger;

public:
    RunPager();
    ~RunPager();
};

}
