#include "logging.hh"
#include "util.hh"

#include <atomic>

namespace nix {

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

    void event(const Event & ev) override
    {
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

Activity::Activity() : id(nextId++) { };

Activity::Activity(ActivityType type, std::string msg)
    : Activity()
{
    logger->event(evStartActivity, id, type, msg);
}

Activity::~Activity()
{
    logger->event(evStopActivity, id);
}

void Activity::progress(uint64_t done, uint64_t expected, uint64_t running, uint64_t failed) const
{
    logger->event(evProgress, id, done, expected, running, failed);
}

}
