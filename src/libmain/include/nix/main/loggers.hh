#pragma once
///@file

#include <string>

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

} // namespace nix
