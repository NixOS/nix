#pragma once

#include "types.hh"

namespace nix {

enum class LogFormat {
  raw,
  rawWithLogs,
  internalJson,
  bar,
  barWithLogs,
};

void setLogFormat(std::string_view logFormatStr);
void setLogFormat(const LogFormat & logFormat);

void createDefaultLogger();

}
