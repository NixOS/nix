#include "loggers.hh"
#include "progress-bar.hh"
#include "util.hh"
#include "globals.hh"

namespace nix {

Logger * makeDefaultLogger() {
    switch (settings.logFormat) {
    case LogFormat::raw:
        return makeSimpleLogger(false);
    case LogFormat::rawWithLogs:
        return makeSimpleLogger(true);
    case LogFormat::internalJson:
        return makeJSONLogger(*makeSimpleLogger(true));
    case LogFormat::bar:
        return makeProgressBar();
    case LogFormat::barWithLogs:
        return makeProgressBar(true);
    default:
        abort();
    }
}

void setLogFormat(const LogFormat & logFormat) {
    settings.logFormat = logFormat;
    createDefaultLogger();
}

void createDefaultLogger() {
    logger = makeDefaultLogger();
}

}
