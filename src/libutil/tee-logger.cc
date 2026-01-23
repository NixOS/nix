#include "nix/util/logging.hh"

namespace nix {

struct TeeLogger : Logger
{
    std::vector<std::unique_ptr<Logger>> loggers;

    TeeLogger(std::vector<std::unique_ptr<Logger>> && loggers)
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

    void log(Verbosity lvl, std::string_view s, const std::string & machine = "") override
    {
        for (auto & logger : loggers)
            logger->log(lvl, s, machine);
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
        ActivityId parent,
        const std::string & machine = "") override
    {
        for (auto & logger : loggers)
            logger->startActivity(act, lvl, type, s, fields, parent, machine);
    }

    void stopActivity(ActivityId act, const std::string & machine = "") override
    {
        for (auto & logger : loggers)
            logger->stopActivity(act, machine);
    }

    void result(ActivityId act, ResultType type, const Fields & fields, const std::string & machine = "") override
    {
        for (auto & logger : loggers)
            logger->result(act, type, fields, machine);
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

std::unique_ptr<Logger>
makeTeeLogger(std::unique_ptr<Logger> mainLogger, std::vector<std::unique_ptr<Logger>> && extraLoggers)
{
    std::vector<std::unique_ptr<Logger>> allLoggers;
    allLoggers.push_back(std::move(mainLogger));
    for (auto & l : extraLoggers)
        allLoggers.push_back(std::move(l));
    return std::make_unique<TeeLogger>(std::move(allLoggers));
}

} // namespace nix
