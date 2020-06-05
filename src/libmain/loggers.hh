#pragma once

#include "types.hh"

namespace nix {

enum class LogFormat {
  raw,
  internalJson,
  bar,
  barWithLogs,
};

void setLogFormat(const string &logFormatStr);
void setLogFormat(const LogFormat &logFormat);

void createDefaultLogger();

}
