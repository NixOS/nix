#include "logging.hh"

namespace nix {

struct TeeLogger : Logger
{
    std::vector<Logger *> loggers;

    TeeLogger(std::vector<Logger *> loggers)
        : loggers(std::move(loggers))
    {
    }

    void stop() override
    {
        for (auto & logger : loggers)
            logger->stop();
    };

    void pause() override
    {
        for (auto & logger : loggers)
            logger->pause();
    };

    void resume() override
    {
        for (auto & logger : loggers)
            logger->resume();
    };

    void log(Verbosity lvl, std::string_view s) override
    {
        for (auto & logger : loggers)
            logger->log(lvl, s);
    }

    void logEI(const ErrorInfo & ei) override
    {
        for (auto & logger : loggers)
            logger->logEI(ei);
    }

    void startActivity(
        ActivityId act,
        Verbosity lvl,
        ActivityType type,
        const std::string & s,
        const Fields & fields,
        ActivityId parent) override
    {
        for (auto & logger : loggers)
            logger->startActivity(act, lvl, type, s, fields, parent);
    }

    void stopActivity(ActivityId act) override
    {
        for (auto & logger : loggers)
            logger->stopActivity(act);
    }

    void result(ActivityId act, ResultType type, const Fields & fields) override
    {
        for (auto & logger : loggers)
            logger->result(act, type, fields);
    }

    void writeToStdout(std::string_view s) override
    {
        for (auto & logger : loggers) {
            /* Let only the first logger write to stdout to avoid
               duplication. This means that the first logger needs to
               be the one managing stdout/stderr
               (e.g. `ProgressBar`). */
            logger->writeToStdout(s);
            break;
        }
    }

    std::optional<char> ask(std::string_view s) override
    {
        for (auto & logger : loggers) {
            auto c = logger->ask(s);
            if (c)
                return c;
        }
        return std::nullopt;
    }

    void setPrintBuildLogs(bool printBuildLogs) override
    {
        for (auto & logger : loggers)
            logger->setPrintBuildLogs(printBuildLogs);
    }
};

Logger * makeTeeLogger(std::vector<Logger *> loggers)
{
    return new TeeLogger(std::move(loggers));
}

}
