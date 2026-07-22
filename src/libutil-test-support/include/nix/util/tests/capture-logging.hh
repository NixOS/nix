#pragma once

#include "nix/util/logging.hh"

#include <sstream>

namespace nix::testing {

class CaptureLogger : public Logger
{
    std::ostringstream oss;

public:
    CaptureLogger() {}

    std::string get() const
    {
        return oss.str();
    }

    void log(Verbosity lvl, std::string_view s) override
    {
        oss << s << std::endl;
    }

    void logEI(const ErrorInfo & ei) override
    {
        showErrorInfo(oss, ei, loggerSettings.showTrace.get());
    }

    std::unique_ptr<Logger> cloneForChild() const override
    {
        /* Not expected to be forked across; a plain logger is fine. */
        return makeSimpleLogger();
    }
};

class CaptureLogging
{
    std::unique_ptr<CaptureLogger> logger;
    Logger * oldLogger;

public:
    CaptureLogging()
    {
        oldLogger = nix::logger;
        logger = std::make_unique<CaptureLogger>();
        nix::logger = logger.get();
    }

    std::string get() const
    {
        return logger->get();
    }

    CaptureLogging(CaptureLogging &&) = delete;
    CaptureLogging(const CaptureLogging &) = delete;
    CaptureLogging & operator=(CaptureLogging &&) = delete;
    CaptureLogging & operator=(const CaptureLogging &) = delete;

    ~CaptureLogging()
    {
        nix::logger = oldLogger;
    }
};

} // namespace nix::testing
