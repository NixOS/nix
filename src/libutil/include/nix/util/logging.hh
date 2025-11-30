#pragma once
///@file

#include "nix/util/error.hh"
#include "nix/util/configuration.hh"
#include "nix/util/file-descriptor.hh"
#include "nix/util/finally.hh"

#include <filesystem>

#include <nlohmann/json_fwd.hpp>

namespace nix {

enum class ActivityType {
    Unknown = 0,
    CopyPath = 100,
    FileTransfer = 101,
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
    FetchTree = 112,
};

enum class ResultType {
    FileLinked = 100,
    BuildLogLine = 101,
    UntrustedPath = 102,
    CorruptedPath = 103,
    SetPhase = 104,
    Progress = 105,
    SetExpected = 106,
    PostBuildLogLine = 107,
    FetchStatus = 108,
};

typedef uint64_t ActivityId;

struct LoggerSettings : Config
{
    Setting<bool> showTrace{
        this,
        false,
        "show-trace",
        R"(
          Whether Nix should print out a stack trace in case of Nix
          expression evaluation errors.
        )"};

    Setting<std::optional<std::filesystem::path>> jsonLogPath{
        this,
        {},
        "json-log-path",
        R"(
          A file or unix socket to which JSON records of Nix's log output are
          written, in the same format as `--log-format internal-json`
          (without the `@nix ` prefixes on each line).
          Concurrent writes to the same file by multiple Nix processes are not supported and
          may result in interleaved or corrupted log records.
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
        enum class Type { Int = 0, String = 1 };
        Type type;

        uint64_t i = 0;
        std::string s;

        Field(const std::string & s)
            : type(Type::String)
            , s(s)
        {
        }

        Field(const char * s)
            : type(Type::String)
            , s(s)
        {
        }

        Field(const uint64_t & i)
            : type(Type::Int)
            , i(i)
        {
        }

        Field(const ActivityType & a)
            : type(Type::Int)
            , i(static_cast<uint64_t>(a))
        {
        }
    };

    typedef std::vector<Field> Fields;

    virtual ~Logger() {}

    virtual void stop() {};

    /**
     * Guard object to resume the logger when done.
     */
    struct Suspension
    {
        Finally<std::function<void()>> _finalize;
    };

    Suspension suspend();

    std::optional<Suspension> suspendIf(bool cond);

    virtual void pause() {};
    virtual void resume() {};

    // Whether the logger prints the whole build log
    virtual bool isVerbose()
    {
        return false;
    }

    virtual void log(Verbosity lvl, std::string_view s) = 0;

    void log(std::string_view s)
    {
        log(Verbosity::Info, s);
    }

    virtual void logEI(const ErrorInfo & ei) = 0;

    void logEI(Verbosity lvl, ErrorInfo ei)
    {
        ei.level = lvl;
        logEI(ei);
    }

    virtual void warn(const std::string & msg);

    virtual void startActivity(
        ActivityId act,
        Verbosity lvl,
        ActivityType type,
        const std::string & s,
        const Fields & fields,
        ActivityId parent) {};

    virtual void stopActivity(ActivityId act) {};

    virtual void result(ActivityId act, ResultType type, const Fields & fields) {};

    virtual void writeToStdout(std::string_view s);

    template<typename... Args>
    inline void cout(const Args &... args)
    {
        writeToStdout(fmt(args...));
    }

    virtual std::optional<char> ask(std::string_view s)
    {
        return {};
    }

    virtual void setPrintBuildLogs(bool printBuildLogs) {}
};

/**
 * A variadic template that does nothing.
 *
 * Useful to call a function with each argument in a parameter pack.
 */
struct nop
{
    template<typename... T>
    nop(T...)
    {
    }
};

ActivityId getCurActivity();
void setCurActivity(const ActivityId activityId);

struct Activity
{
    Logger & logger;

    const ActivityId id;

    Activity(
        Logger & logger,
        Verbosity lvl,
        ActivityType type,
        const std::string & s = "",
        const Logger::Fields & fields = {},
        ActivityId parent = getCurActivity());

    Activity(
        Logger & logger, ActivityType type, const Logger::Fields & fields = {}, ActivityId parent = getCurActivity())
        : Activity(logger, Verbosity::Error, type, "", fields, parent) {};

    Activity(const Activity & act) = delete;

    ~Activity();

    void progress(uint64_t done = 0, uint64_t expected = 0, uint64_t running = 0, uint64_t failed = 0) const
    {
        result(ResultType::Progress, done, expected, running, failed);
    }

    void setExpected(ActivityType type2, uint64_t expected) const
    {
        result(ResultType::SetExpected, type2, expected);
    }

    template<typename... Args>
    void result(ResultType type, const Args &... args) const
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

    PushActivity(ActivityId act)
        : prevAct(getCurActivity())
    {
        setCurActivity(act);
    }

    ~PushActivity()
    {
        setCurActivity(prevAct);
    }
};

extern std::unique_ptr<Logger> logger;

std::unique_ptr<Logger> makeSimpleLogger(bool printBuildLogs = true);

/**
 * Create a logger that sends log messages to `mainLogger` and the
 * list of loggers in `extraLoggers`. Only `mainLogger` is used for
 * writing to stdout and getting user input.
 */
std::unique_ptr<Logger>
makeTeeLogger(std::unique_ptr<Logger> mainLogger, std::vector<std::unique_ptr<Logger>> && extraLoggers);

std::unique_ptr<Logger> makeJSONLogger(Descriptor fd, bool includeNixPrefix = true);

std::unique_ptr<Logger> makeJSONLogger(const std::filesystem::path & path, bool includeNixPrefix = true);

void applyJSONLogger();

/**
 * @param source A noun phrase describing the source of the message, e.g. "the builder".
 */
std::optional<nlohmann::json> parseJSONMessage(const std::string & msg, std::string_view source);

/**
 * @param source A noun phrase describing the source of the message, e.g. "the builder".
 */
bool handleJSONLogMessage(
    nlohmann::json & json,
    const Activity & act,
    std::map<ActivityId, Activity> & activities,
    std::string_view source,
    bool trusted);

/**
 * @param source A noun phrase describing the source of the message, e.g. "the builder".
 */
bool handleJSONLogMessage(
    const std::string & msg,
    const Activity & act,
    std::map<ActivityId, Activity> & activities,
    std::string_view source,
    bool trusted);

/**
 * suppress msgs > this
 */
extern Verbosity verbosity;

/**
 * Print a message with the standard ErrorInfo format.
 * In general, use these 'log' macros for reporting problems that may require user
 * intervention or that need more explanation.  Use the 'print' macros for more
 * lightweight status messages.
 */
#define logErrorInfo(level, errorInfo...)      \
    do {                                       \
        if ((level) <= nix::verbosity) {       \
            logger->logEI((level), errorInfo); \
        }                                      \
    } while (0)

#define logError(errorInfo...) logErrorInfo(Verbosity::Error, errorInfo)
#define logWarning(errorInfo...) logErrorInfo(Verbosity::Warn, errorInfo)

/**
 * Print a string message if the current log level is at least the specified
 * level. Note that this has to be implemented as a macro to ensure that the
 * arguments are evaluated lazily.
 */
#define printMsgUsing(loggerParam, level, args...) \
    do {                                           \
        auto __lvl = level;                        \
        if (__lvl <= nix::verbosity) {             \
            loggerParam->log(__lvl, fmt(args));    \
        }                                          \
    } while (0)
#define printMsg(level, args...) printMsgUsing(logger, level, args)

#define printError(args...) printMsg(Verbosity::Error, args)
#define notice(args...) printMsg(Verbosity::Notice, args)
#define printInfo(args...) printMsg(Verbosity::Info, args)
#define printTalkative(args...) printMsg(Verbosity::Talkative, args)
#define debug(args...) printMsg(Verbosity::Debug, args)
#define vomit(args...) printMsg(Verbosity::Vomit, args)

/**
 * if verbosity >= Verbosity::Warn, print a message with a yellow 'warning:' prefix.
 */
template<typename... Args>
inline void warn(const std::string & fs, const Args &... args)
{
    boost::format f(fs);
    formatHelper(f, args...);
    logger->warn(f.str());
}

#define warnOnce(haveWarned, args...) \
    if (!haveWarned) {                \
        haveWarned = true;            \
        warn(args);                   \
    }

void writeToStderr(std::string_view s);

} // namespace nix
