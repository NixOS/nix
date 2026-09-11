#include "nix/util/logging.hh"
#include "nix/util/file-descriptor.hh"
#include "nix/util/environment-variables.hh"
#include "nix/util/terminal.hh"
#include "nix/util/util.hh"
#include "nix/util/config-global.hh"
#include "nix/util/position.hh"
#include "nix/util/sync.hh"
#include "nix/util/unix-domain-socket.hh"
#include "nix/util/json-utils.hh"

#include <atomic>
#include <sstream>
#include <nlohmann/json.hpp>

namespace nix {

void LoggerSettings::anchor() {}

LoggerSettings loggerSettings;

static GlobalConfig::Register rLoggerSettings(&loggerSettings);

static thread_local ActivityId curActivity = 0;

ActivityId getCurActivity()
{
    return curActivity;
}

void setCurActivity(const ActivityId activityId)
{
    curActivity = activityId;
}

/**
 * This is a raw pointer to allow it to leak.
 * Avoids races in activity teardown.
 */
Logger * logger = makeSimpleLogger(true).release();

Logger::~Logger() {}

void Logger::warn(const std::string & msg) noexcept
{
    log(lvlWarn, ANSI_WARNING "warning:" ANSI_NORMAL " " + msg);
}

void Logger::writeToStdout(std::string_view s)
{
    Descriptor standard_out = getStandardOutput();
    writeFull(standard_out, s);
    writeFull(standard_out, "\n");
}

Logger::Suspension Logger::suspend()
{
    pause();
    return Suspension{._finalize = {[this]() { this->resume(); }}};
}

std::optional<Logger::Suspension> Logger::suspendIf(bool cond)
{
    if (cond)
        return suspend();
    return {};
}

namespace {

class SimpleLogger : public Logger
{
public:

    bool systemd, tty;
    bool printBuildLogs;

    SimpleLogger(bool printBuildLogs)
        : printBuildLogs(printBuildLogs)
    {
        systemd = getEnv("IN_SYSTEMD") == "1";
        tty = isTTY();
    }

    bool isVerbose() override
    {
        return printBuildLogs;
    }

    void log(Verbosity lvl, std::string_view s) noexcept override
    {
        if (lvl > verbosity)
            return;

        std::string prefix;

        if (systemd) {
            char c;
            switch (lvl) {
            case lvlError:
                c = '3';
                break;
            case lvlWarn:
                c = '4';
                break;
            case lvlNotice:
            case lvlInfo:
                c = '5';
                break;
            case lvlTalkative:
            case lvlChatty:
                c = '6';
                break;
            case lvlDebug:
            case lvlVomit:
                c = '7';
                break;
            default:
                c = '7';
                break; // should not happen, and missing enum case is reported by -Werror=switch-enum
            }
            prefix = std::string("<") + c + ">";
        }

        writeToStderr(prefix + filterANSIEscapes(s, !tty) + "\n");
    }

    void logEI(const ErrorInfo & ei) noexcept override
    {
        std::ostringstream oss;
        showErrorInfo(oss, ei, loggerSettings.showTrace.get());

        log(ei.level, oss.view());
    }

    void startActivity(
        ActivityId act,
        Verbosity lvl,
        ActivityType type,
        const std::string & s,
        std::span<const Field> fields,
        ActivityId parent) noexcept override
    {
        if (lvl <= verbosity && !s.empty())
            log(lvl, s + "...");
    }

    void result(ActivityId act, ResultType type, std::span<const Field> fields) noexcept override
    {
        if (type == resBuildLogLine && printBuildLogs) {
            const auto * lastLine = std::get_if<std::string>(&fields[0]);
            if (lastLine)
                printError(*lastLine);
        } else if (type == resPostBuildLogLine && printBuildLogs) {
            const auto * lastLine = std::get_if<std::string>(&fields[0]);
            if (lastLine)
                printError("post-build-hook: " + *lastLine);
        }
    }
};

} // namespace

Verbosity verbosity = lvlInfo;

static void writeFullLogging(Descriptor fd, std::string_view s) noexcept
{
    try {
        writeFull(fd, s, false);
    } catch (SystemError & e) {
        /* Ignore failing logging writes.  We need to ignore write
           errors to ensure that cleanup code that writes logs runs
           to completion if the other side of the logging fd has
           been closed unexpectedly. */
    }
}

void writeToStderr(std::string_view s) noexcept
{
    writeFullLogging(getStandardError(), s);
}

std::unique_ptr<Logger> makeSimpleLogger(bool printBuildLogs)
{
    return std::make_unique<SimpleLogger>(printBuildLogs);
}

std::atomic<uint64_t> nextId{0};

static uint64_t getPid()
{
#ifndef _WIN32
    return getpid();
#else
    return GetCurrentProcessId();
#endif
}

Activity::Activity(
    Logger & logger,
    Verbosity lvl,
    ActivityType type,
    const std::string & s,
    std::span<const Logger::Field> fields,
    ActivityId parent)
    : logger(logger)
    , id(nextId++ + (((uint64_t) getPid()) << 32))
{
    logger.startActivity(id, lvl, type, s, fields, parent);
}

void to_json(nlohmann::json & json, std::shared_ptr<const Pos> pos)
{
    if (pos) {
        json["line"] = pos->line;
        json["column"] = pos->column;
        std::ostringstream str;
        pos->print(str, true);
        json["file"] = str.str();
    } else {
        json["line"] = nullptr;
        json["column"] = nullptr;
        json["file"] = nullptr;
    }
}

namespace {

struct JSONLogger : Logger
{
    Descriptor fd;
    bool includeNixPrefix;

    JSONLogger(Descriptor fd, bool includeNixPrefix)
        : fd(fd)
        , includeNixPrefix(includeNixPrefix)
    {
    }

    bool isVerbose() override
    {
        return true;
    }

    void addFields(nlohmann::json & json, std::span<const Field> fields)
    {
        if (fields.empty())
            return;
        auto & arr = json["fields"] = nlohmann::json::array();
        for (auto & f : fields)
            std::visit([&arr](const auto & v) { arr.push_back(v); }, f);
    }

    struct State
    {
        bool enabled = true;
    };

    Sync<State> _state;

    void write(const nlohmann::json & json)
    {
        auto line = (includeNixPrefix ? "@nix " : "")
                    + json.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace) + "\n";

        /* Acquire a lock to prevent log messages from clobbering each
           other. */
        try {
            auto state(_state.lock());
            if (state->enabled)
                writeFullLogging(fd, line);
        } catch (...) {
            bool enabled = false;
            std::swap(_state.lock()->enabled, enabled);
            if (enabled) {
                ignoreExceptionExceptInterrupt();
                logger->warn("disabling JSON logger due to write errors");
            }
        }
    }

    void log(Verbosity lvl, std::string_view s) noexcept override
    {
        nlohmann::json json;
        json["action"] = "msg";
        json["level"] = lvl;
        json["msg"] = s;
        write(json);
    }

    void logEI(const ErrorInfo & ei) noexcept override
    {
        std::ostringstream oss;
        showErrorInfo(oss, ei, loggerSettings.showTrace.get());

        nlohmann::json json;
        json["action"] = "msg";
        json["level"] = ei.level;
        json["msg"] = oss.str();
        json["raw_msg"] = ei.msg.str();
        to_json(json, ei.pos);

        if (loggerSettings.showTrace.get() && !ei.traces.empty()) {
            nlohmann::json traces = nlohmann::json::array();
            for (auto iter = ei.traces.rbegin(); iter != ei.traces.rend(); ++iter) {
                nlohmann::json stackFrame;
                stackFrame["raw_msg"] = iter->hint.str();
                to_json(stackFrame, iter->pos);
                traces.push_back(stackFrame);
            }

            json["trace"] = traces;
        }

        write(json);
    }

    void startActivity(
        ActivityId act,
        Verbosity lvl,
        ActivityType type,
        const std::string & s,
        std::span<const Field> fields,
        ActivityId parent) noexcept override
    {
        nlohmann::json json;
        json["action"] = "start";
        json["id"] = act;
        json["level"] = lvl;
        json["type"] = type;
        json["text"] = s;
        json["parent"] = parent;
        addFields(json, fields);
        write(json);
    }

    void stopActivity(ActivityId act) noexcept override
    {
        nlohmann::json json;
        json["action"] = "stop";
        json["id"] = act;
        write(json);
    }

    void result(ActivityId act, ResultType type, std::span<const Field> fields) noexcept override
    {
        nlohmann::json json;
        json["action"] = "result";
        json["id"] = act;
        json["type"] = type;
        addFields(json, fields);
        write(json);
    }
};

} // namespace

std::unique_ptr<Logger> makeJSONLogger(Descriptor fd, bool includeNixPrefix)
{
    return std::make_unique<JSONLogger>(fd, includeNixPrefix);
}

std::unique_ptr<Logger> makeJSONLogger(const std::filesystem::path & path, bool includeNixPrefix)
{
    struct JSONFileLogger : JSONLogger
    {
        AutoCloseFD fd;

        JSONFileLogger(AutoCloseFD && fd, bool includeNixPrefix)
            : JSONLogger(fd.get(), includeNixPrefix)
            , fd(std::move(fd))
        {
        }
    };

    AutoCloseFD fd = std::filesystem::is_socket(path) ? connect(path)
                                                      : toDescriptor(open(
                                                            path.string().c_str(),
                                                            O_CREAT | O_APPEND | O_WRONLY
#ifndef _WIN32
                                                                | O_CLOEXEC
#endif
                                                            ,
                                                            0644));
    if (!fd)
        throw SysError("opening log file %1%", PathFmt(path));

    return std::make_unique<JSONFileLogger>(std::move(fd), includeNixPrefix);
}

void applyJSONLogger()
{
    if (auto & opt = loggerSettings.jsonLogPath.get()) {
        try {
            std::vector<std::unique_ptr<Logger>> loggers;
            loggers.push_back(makeJSONLogger(*opt, false));
            try {
                logger = makeTeeLogger(std::unique_ptr<Logger>(logger), std::move(loggers)).release();
            } catch (...) {
                // `logger` is now gone so give up.
                abort();
            }
        } catch (...) {
            ignoreExceptionExceptInterrupt();
        }
    }
}

static auto getFields(const nlohmann::json::array_t & json)
{
    std::vector<Logger::Field> fields;
    for (const auto & f : json) {
        if (f.type() == nlohmann::json::value_t::number_unsigned)
            fields.emplace_back(Logger::Field(getUnsigned(f)));
        else if (f.type() == nlohmann::json::value_t::string)
            fields.emplace_back(Logger::Field(getString(f)));
        else
            throw Error("unsupported JSON type %d", (int) f.type());
    }
    return fields;
}

static std::vector<Logger::Field> maybeGetFields(const nlohmann::json::array_t * json)
{
    if (!json)
        return {};
    return getFields(*json);
}

std::optional<nlohmann::json> parseJSONMessage(std::string_view msg, std::string_view source)
{
    if (!hasPrefix(msg, "@nix "))
        return std::nullopt;
    try {
        return nlohmann::json::parse(msg.substr(5));
    } catch (std::exception & e) {
        printError("bad JSON log message from %s: %s", Uncolored(source), e.what());
    }
    return std::nullopt;
}

bool handleJSONLogMessage(
    const nlohmann::json & rawJson,
    const Activity & act,
    std::map<ActivityId, Activity> & activities,
    std::string_view source,
    bool trusted)
try {
    using namespace std::string_view_literals;

    auto & json = getObject(rawJson);
    std::string action = getString(valueAt(json, "action"sv));

    if (action == "start"sv) {
        auto rawType = getUnsigned(valueAt(json, "type"sv));
        if (rawType != actUnknown && (rawType < actCopyPath || rawType > actLast))
            throw Error("unknown activity type %d", rawType);
        auto type = static_cast<ActivityType>(rawType);
        if (trusted || type == actFileTransfer) {
            auto level = verbosityFromIntClamped(getUnsigned(valueAt(json, "level"sv)));
            auto id = getUnsigned(valueAt(json, "id"sv));
            auto maybeFieldsValue = optionalValueAt(json, "fields"sv);
            /* Back-compat, lack of "fields" member would silently translate into an empty
               array of fields. */
            auto fields = maybeGetFields(maybeFieldsValue ? &getArray(*maybeFieldsValue) : nullptr);
            activities.emplace(
                std::piecewise_construct,
                std::forward_as_tuple(id),
                std::forward_as_tuple(*logger, level, type, getString(valueAt(json, "text"sv)), fields, act.id));
        }
    }

    else if (action == "stop"sv)
        activities.erase(getUnsigned(valueAt(json, "id"sv)));

    else if (action == "result"sv) {
        ActivityId id = getUnsigned(valueAt(json, "id"sv));
        auto i = activities.find(id);
        auto rawType = getUnsigned(valueAt(json, "type"sv));
        if (rawType < resFileLinked || rawType > resLast)
            throw Error("unknown result type %d", rawType);
        auto maybeFieldsValue = optionalValueAt(json, "fields"sv);
        /* Back-compat, lack of "fields" member would silently translate into an empty
           array of fields. */
        auto fields = maybeGetFields(maybeFieldsValue ? &getArray(*maybeFieldsValue) : nullptr);
        if (i != activities.end())
            i->second.result(static_cast<ResultType>(rawType), fields);
    }

    else if (action == "setPhase"sv) {
        std::string phase = getString(valueAt(json, "phase"sv));
        act.result(resSetPhase, phase);
    }

    else if (action == "msg"sv) {
        auto level = verbosityFromIntClamped(getUnsigned(valueAt(json, "level"sv)));
        logger->log(level, getString(valueAt(json, "msg"sv)));
    }

    /* TODO: Don't ignore extra fields? Or keep on ignoring for forwards compatibility? */

    return true;
} catch (const nix::Error & e) {
    warn("unable to handle a JSON message from %s: %s", Uncolored(source), e.message());
    return false;
} catch (const nlohmann::json::exception & e) {
    warn("unable to handle a JSON message from %s: %s", Uncolored(source), e.what());
    return false;
}

bool handleJSONLogMessage(
    std::string_view msg,
    const Activity & act,
    std::map<ActivityId, Activity> & activities,
    std::string_view source,
    bool trusted)
{
    const auto json = parseJSONMessage(msg, source);
    if (!json)
        return false;

    return handleJSONLogMessage(*json, act, activities, source, trusted);
}

Activity::~Activity()
{
    try {
        logger.stopActivity(id);
    } catch (...) {
        ignoreExceptionInDestructor();
    }
}

} // namespace nix
