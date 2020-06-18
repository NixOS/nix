#pragma once

#include "types.hh"
#include "error.hh"

namespace nix {

enum struct ActivityType {
    Unknown = 0,
    CopyPath = 100,
    Download = 101,
    Realise = 102,
    CopyPaths = 103,
    Builds = 104,
    Build = 105,
    OptimiseStore = 106,
    VerifyPaths = 107,
    Substitute = 108,
    QueryPathInfo = 109,
    PostBuildHook = 110,
    BuildWaiting = 111,
};

enum struct ResultType {
    FileLinked = 100,
    BuildLogLine = 101,
    UntrustedPath = 102,
    CorruptedPath = 103,
    SetPhase = 104,
    Progress = 105,
    SetExpected = 106,
    PostBuildLogLine = 107,
};

typedef uint64_t ActivityId;

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
        log(Verbosity::Info, fs);
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
    inline void stdout(const std::string & fs, const Args & ... args)
    {
        boost::format f(fs);
        formatHelper(f, args...);
        writeToStdout(f.str());
    }
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
        : Activity(logger, Verbosity::Error, type, "", fields, parent) { };

    Activity(const Activity & act) = delete;

    ~Activity();

    void progress(uint64_t done = 0, uint64_t expected = 0, uint64_t running = 0, uint64_t failed = 0) const
    { result(ResultType::Progress, done, expected, running, failed); }

    void setExpected(ActivityType type2, uint64_t expected) const
    { result(ResultType::SetExpected, (uint64_t)type2, expected); }

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
        if (level <= nix::verbosity) { \
            logger->logEI(level, errorInfo); \
        } \
    } while (0)

#define logError(errorInfo...) logErrorInfo(Verbosity::Error, errorInfo)
#define logWarning(errorInfo...) logErrorInfo(Verbosity::Warn, errorInfo)

/* Print a string message if the current log level is at least the specified
   level. Note that this has to be implemented as a macro to ensure that the
   arguments are evaluated lazily. */
#define printMsg(level, args...) \
    do { \
        if (level <= nix::verbosity) { \
            logger->log(level, fmt(args)); \
        } \
    } while (0)

#define printError(args...) printMsg(Verbosity::Error, args)
#define printInfo(args...) printMsg(Verbosity::Info, args)
#define printTalkative(args...) printMsg(Verbosity::Talkative, args)
#define debug(args...) printMsg(Verbosity::Debug, args)
#define vomit(args...) printMsg(Verbosity::Vomit, args)

/* if verbosity >= Verbosity::Warn, print a message with a yellow 'warning:' prefix. */
template<typename... Args>
inline void warn(const std::string & fs, const Args & ... args)
{
    boost::format f(fs);
    formatHelper(f, args...);
    logger->warn(f.str());
}

void warnOnce(bool & haveWarned, const FormatOrString & fs);

void writeToStderr(const string & s);

}
