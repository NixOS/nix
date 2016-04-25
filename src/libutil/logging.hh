#pragma once

#include "types.hh"

namespace nix {

typedef enum {
    lvlError = 0,
    lvlInfo,
    lvlTalkative,
    lvlChatty,
    lvlDebug,
    lvlVomit
} Verbosity;

class Activity;

class Logger
{
    friend class Activity;

public:

    virtual ~Logger() { }

    virtual void log(Verbosity lvl, const FormatOrString & fs) = 0;

    void log(const FormatOrString & fs)
    {
        log(lvlInfo, fs);
    }

    virtual void setExpected(const std::string & label, uint64_t value = 1) { }
    virtual void setProgress(const std::string & label, uint64_t value = 1) { }
    virtual void incExpected(const std::string & label, uint64_t value = 1) { }
    virtual void incProgress(const std::string & label, uint64_t value = 1) { }

private:

    virtual void startActivity(Activity & activity, Verbosity lvl, const FormatOrString & fs) = 0;

    virtual void stopActivity(Activity & activity) = 0;

};

class Activity
{
public:
    Logger & logger;

    Activity(Logger & logger, Verbosity lvl, const FormatOrString & fs)
        : logger(logger)
    {
        logger.startActivity(*this, lvl, fs);
    }

    ~Activity()
    {
        logger.stopActivity(*this);
    }
};

extern Logger * logger;

Logger * makeDefaultLogger();

extern Verbosity verbosity; /* suppress msgs > this */

#define printMsg(level, f) \
    do { \
        if (level <= nix::verbosity) { \
            logger->log(level, (f)); \
        } \
    } while (0)

#define debug(f) printMsg(lvlDebug, f)

void warnOnce(bool & haveWarned, const FormatOrString & fs);

void writeToStderr(const string & s);

}
