#pragma once

#include "types.hh"
#include "error.hh"
#include "config.hh"

#include <nlohmann/json_fwd.hpp>

namespace nix {

typedef enum {
    actUnknown = 0,
    actCopyPath = 100,
    actFileTransfer = 101,
    actRealise = 102,
    actCopyPaths = 103,
    actBuilds = 104,
    actBuild = 105,
    actOptimiseStore = 106,
    actVerifyPaths = 107,
    actSubstitute = 108,
    actQueryPathInfo = 109,
    actPostBuildHook = 110,
    actBuildWaiting = 111,
} ActivityType;

typedef enum {
    resFileLinked = 100,
    resBuildLogLine = 101,
    resUntrustedPath = 102,
    resCorruptedPath = 103,
    resSetPhase = 104,
    resProgress = 105,
    resSetExpected = 106,
    resPostBuildLogLine = 107,
} ResultType;

typedef uint64_t ActivityId;

struct LoggerSettings : Config
{
    Setting<bool> showTrace{
        this, false, "show-trace",
        R"(
          Whether Nix should print out a stack trace in case of Nix
          expression evaluation errors.
        )"};
};

extern LoggerSettings loggerSettings;

class Logger
{
    friend struct Activity;

public:

    struct Field
    {
        // FIXME: use std::variant.
        enum { tInt = 0, tString = 1 } type;
        uint64_t i = 0;
        std::string s;
        Field(const std::string & s) : type(tString), s(s) { }
        Field(const char * s) : type(tString), s(s) { }
        Field(const uint64_t & i) : type(tInt), i(i) { }
    };

    typedef std::vector<Field> Fields;

    virtual ~Logger() { }

    virtual void stop() { };

    // Whether the logger prints the whole build log
    virtual bool isVerbose() { return false; }

    virtual void log(Verbosity lvl, const FormatOrString & fs) = 0;

    void log(const FormatOrString & fs)
    {
        log(lvlInfo, fs);
    }

    virtual void logEI(const ErrorInfo &ei) = 0;

    void logEI(Verbosity lvl, ErrorInfo ei)
    {
        ei.level = lvl;
        logEI(ei);
    }

    virtual void warn(const std::string & msg);

    virtual void startActivity(ActivityId act, Verbosity lvl, ActivityType type,
        const std::string & s, const Fields & fields, ActivityId parent) { };

    virtual void stopActivity(ActivityId act) { };

    virtual void result(ActivityId act, ResultType type, const Fields & fields) { };

    virtual void writeToStdout(std::string_view s);

    template<typename... Args>
    inline void cout(const std::string & fs, const Args & ... args)
    {
        boost::format f(fs);
        formatHelper(f, args...);
        writeToStdout(f.str());
    }

    virtual std::optional<char> ask(std::string_view s)
    { return {}; }
};

ActivityId getCurActivity();
void setCurActivity(const ActivityId activityId);

struct Activity
{
    Logger & logger;

    const ActivityId id;

    Activity(Logger & logger, Verbosity lvl, ActivityType type, const std::string & s = "",
        const Logger::Fields & fields = {}, ActivityId parent = getCurActivity());

    Activity(Logger & logger, ActivityType type,
        const Logger::Fields & fields = {}, ActivityId parent = getCurActivity())
        : Activity(logger, lvlError, type, "", fields, parent) { };

    Activity(const Activity & act) = delete;

    ~Activity();

    void progress(uint64_t done = 0, uint64_t expected = 0, uint64_t running = 0, uint64_t failed = 0) const
    { result(resProgress, done, expected, running, failed); }

    void setExpected(ActivityType type2, uint64_t expected) const
    { result(resSetExpected, type2, expected); }

    template<typename... Args>
    void result(ResultType type, const Args & ... args) const
    {
        Logger::Fields fields;
        nop{(fields.emplace_back(Logger::Field(args)), 1)...};
        result(type, fields);
    }

    void result(ResultType type, const Logger::Fields & fields) const
    {
        logger.result(id, type, fields);
    }

    friend class Logger;
};

struct PushActivity
{
    const ActivityId prevAct;
    PushActivity(ActivityId act) : prevAct(getCurActivity()) { setCurActivity(act); }
    ~PushActivity() { setCurActivity(prevAct); }
};

extern Logger * logger;

Logger * makeSimpleLogger(bool printBuildLogs = true);

Logger * makeJSONLogger(Logger & prevLogger);

std::optional<nlohmann::json> parseJSONMessage(const std::string & msg);

bool handleJSONLogMessage(nlohmann::json & json,
    const Activity & act, std::map<ActivityId, Activity> & activities,
    bool trusted);

bool handleJSONLogMessage(const std::string & msg,
    const Activity & act, std::map<ActivityId, Activity> & activities,
    bool trusted);

extern Verbosity verbosity; /* suppress msgs > this */

/* Print a message with the standard ErrorInfo format.
   In general, use these 'log' macros for reporting problems that may require user
   intervention or that need more explanation.  Use the 'print' macros for more
   lightweight status messages. */
#define logErrorInfo(level, errorInfo...) \
    do { \
        if ((level) <= nix::verbosity) {     \
            logger->logEI((level), errorInfo);  \
        } \
    } while (0)

#define logError(errorInfo...) logErrorInfo(lvlError, errorInfo)
#define logWarning(errorInfo...) logErrorInfo(lvlWarn, errorInfo)

/* Print a string message if the current log level is at least the specified
   level. Note that this has to be implemented as a macro to ensure that the
   arguments are evaluated lazily. */
#define printMsgUsing(loggerParam, level, args...) \
    do { \
        auto __lvl = level; \
        if (__lvl <= nix::verbosity) { \
            loggerParam->log(__lvl, fmt(args)); \
        } \
    } while (0)
#define printMsg(level, args...) printMsgUsing(logger, level, args)

#define printError(args...) printMsg(lvlError, args)
#define notice(args...) printMsg(lvlNotice, args)
#define printInfo(args...) printMsg(lvlInfo, args)
#define printTalkative(args...) printMsg(lvlTalkative, args)
#define debug(args...) printMsg(lvlDebug, args)
#define vomit(args...) printMsg(lvlVomit, args)

/* if verbosity >= lvlWarn, print a message with a yellow 'warning:' prefix. */
template<typename... Args>
inline void warn(const std::string & fs, const Args & ... args)
{
    boost::format f(fs);
    formatHelper(f, args...);
    logger->warn(f.str());
}

void warnOnce(bool & haveWarned, const FormatOrString & fs);

void writeToStderr(std::string_view s);

}
