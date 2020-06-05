#include "logging.hh"
#include "util.hh"

#include <atomic>

namespace nix {

thread_local ActivityId curActivity = 0;

Logger * logger = makeDefaultLogger();

void Logger::warn(const std::string & msg)
{
    log(lvlInfo, ANSI_RED "warning:" ANSI_NORMAL " " + msg);
}

class SimpleLogger : public Logger
{
public:

    bool systemd, tty;

    SimpleLogger()
    {
        systemd = getEnv("IN_SYSTEMD") == "1";
        tty = isatty(STDERR_FILENO);
    }

    void log(Verbosity lvl, const FormatOrString & fs) override
    {
        if (lvl > verbosity) return;

        std::string prefix;

        if (systemd) {
            char c;
            switch (lvl) {
            case lvlError: c = '3'; break;
            case lvlInfo: c = '5'; break;
            case lvlTalkative: case lvlChatty: c = '6'; break;
            default: c = '7';
            }
            prefix = std::string("<") + c + ">";
        }

        writeToStderr(prefix + (tty ? fs.s : filterANSIEscapes(fs.s)) + "\n");
    }

    void startActivity(ActivityId act, Verbosity lvl, ActivityType type,
        const std::string & s, const Fields & fields, ActivityId parent)
    {
        if (lvl <= verbosity && !s.empty())
            log(lvl, s + "...");
    }
};

Verbosity verbosity = lvlInfo;

void warnOnce(bool & haveWarned, const FormatOrString & fs)
{
    if (!haveWarned) {
        warn(fs.s);
        haveWarned = true;
    }
}

void writeToStderr(const string & s)
{
    try {
        writeFull(STDERR_FILENO, s, false);
    } catch (SysError & e) {
        /* Ignore failing writes to stderr.  We need to ignore write
           errors to ensure that cleanup code that logs to stderr runs
           to completion if the other side of stderr has been closed
           unexpectedly. */
    }
}

Logger * makeDefaultLogger()
{
    return new SimpleLogger();
}

std::atomic<uint64_t> nextId{(uint64_t) getpid() << 32};

Activity::Activity(Logger & logger, Verbosity lvl, ActivityType type,
    const std::string & s, const Logger::Fields & fields, ActivityId parent)
    : logger(logger), id(nextId++)
{
    logger.startActivity(id, lvl, type, s, fields, parent);
}

}
