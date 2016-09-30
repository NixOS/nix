#include "logging.hh"
#include "util.hh"

namespace nix {

Logger * logger = 0;

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

    void startActivity(Activity & activity, Verbosity lvl, const FormatOrString & fs) override
    {
        log(lvl, fs);
    }

    void stopActivity(Activity & activity) override
    {
    }
};

Verbosity verbosity = lvlInfo;

void warnOnce(bool & haveWarned, const FormatOrString & fs)
{
    if (!haveWarned) {
        printError(format("warning: %1%") % fs.s);
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

}
