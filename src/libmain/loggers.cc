#include "loggers.hh"
#include "environment-variables.hh"
#include "progress-bar.hh"

namespace nix {

LogFormat defaultLogFormat = LogFormat::raw;

LogFormat parseLogFormat(const std::string & logFormatStr) {
    if (logFormatStr == "raw" || getEnv("NIX_GET_COMPLETIONS"))
        return LogFormat::raw;
    else if (logFormatStr == "raw-with-logs")
        return LogFormat::rawWithLogs;
    else if (logFormatStr == "internal-json")
        return LogFormat::internalJSON;
    else if (logFormatStr == "bar")
        return LogFormat::bar;
    throw Error("option 'log-format' has an invalid value '%s'", logFormatStr);
}

Logger * makeDefaultLogger() {
    switch (defaultLogFormat) {
    case LogFormat::raw:
        return makeSimpleLogger(false);
    case LogFormat::rawWithLogs:
        return makeSimpleLogger(true);
    case LogFormat::internalJSON:
        return makeJSONLogger(*makeSimpleLogger(true));
    case LogFormat::bar:
        return makeProgressBar();
    default:
        unreachable();
    }
}

void setLogFormat(const std::string & logFormatStr) {
    setLogFormat(parseLogFormat(logFormatStr));
}

void setLogFormat(const LogFormat & logFormat) {
    defaultLogFormat = logFormat;
    createDefaultLogger();
}

void createDefaultLogger() {
    logger = makeDefaultLogger();
}

}
