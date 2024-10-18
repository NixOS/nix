#pragma once
///@file

#include "types.hh"

namespace nix {

enum class LogFormat {
  raw,
  rawWithLogs,
  internalJSON,
  bar,
  barWithLogs,
  json,
};

void setLogFormat(const std::string & logFormatStr);
void setLogFormat(const LogFormat & logFormat);

void createDefaultLogger();

}
