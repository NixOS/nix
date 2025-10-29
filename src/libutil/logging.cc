#include "nix/util/logging.hh"
#include "nix/util/file-descriptor.hh"
#include "nix/util/environment-variables.hh"
#include "nix/util/terminal.hh"
#include "nix/util/util.hh"
#include "nix/util/config-global.hh"
#include "nix/util/source-path.hh"
#include "nix/util/position.hh"
#include "nix/util/sync.hh"
#include "nix/util/unix-domain-socket.hh"

#include <atomic>
#include <sstream>
#include <nlohmann/json.hpp>
#include <iostream>

namespace nix {

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

std::unique_ptr<Logger> logger = makeSimpleLogger(true);

void Logger::warn(const std::string & msg)
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

    void log(Verbosity lvl, std::string_view s) override
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

    void logEI(const ErrorInfo & ei) override
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
        const Fields & fields,
        ActivityId parent) override
    {
        if (lvl <= verbosity && !s.empty())
            log(lvl, s + "...");
    }

    void result(ActivityId act, ResultType type, const Fields & fields) override
    {
        if (type == resBuildLogLine && printBuildLogs) {
            auto lastLine = fields[0].s;
            printError(lastLine);
        } else if (type == resPostBuildLogLine && printBuildLogs) {
            auto lastLine = fields[0].s;
            printError("post-build-hook: " + lastLine);
        }
    }
};

Verbosity verbosity = lvlInfo;

void writeToStderr(std::string_view s)
{
    try {
        writeFull(getStandardError(), s, false);
    } catch (SystemError & e) {
        /* Ignore failing writes to stderr.  We need to ignore write
           errors to ensure that cleanup code that logs to stderr runs
           to completion if the other side of stderr has been closed
           unexpectedly. */
    }
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
    const Logger::Fields & fields,
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

    void addFields(nlohmann::json & json, const Fields & fields)
    {
        if (fields.empty())
            return;
        auto & arr = json["fields"] = nlohmann::json::array();
        for (auto & f : fields)
            if (f.type == Logger::Field::tInt)
                arr.push_back(f.i);
            else if (f.type == Logger::Field::tString)
                arr.push_back(f.s);
            else
                unreachable();
    }

    struct State
    {
        bool enabled = true;
    };

    Sync<State> _state;

    void write(const nlohmann::json & json)
    {
        auto line =
            (includeNixPrefix ? "@nix " : "") + json.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);

        /* Acquire a lock to prevent log messages from clobbering each
           other. */
        try {
            auto state(_state.lock());
            if (state->enabled)
                writeLine(fd, line);
        } catch (...) {
            bool enabled = false;
            std::swap(_state.lock()->enabled, enabled);
            if (enabled) {
                ignoreExceptionExceptInterrupt();
                logger->warn("disabling JSON logger due to write errors");
            }
        }
    }

    void log(Verbosity lvl, std::string_view s) override
    {
        nlohmann::json json;
        json["action"] = "msg";
        json["level"] = lvl;
        json["msg"] = s;
        write(json);
    }

    void logEI(const ErrorInfo & ei) override
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
        const Fields & fields,
        ActivityId parent) override
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

    void stopActivity(ActivityId act) override
    {
        nlohmann::json json;
        json["action"] = "stop";
        json["id"] = act;
        write(json);
    }

    void result(ActivityId act, ResultType type, const Fields & fields) override
    {
        nlohmann::json json;
        json["action"] = "result";
        json["id"] = act;
        json["type"] = type;
        addFields(json, fields);
        write(json);
    }
};

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

    AutoCloseFD fd = std::filesystem::is_socket(path)
                         ? connect(path)
                         : toDescriptor(open(path.string().c_str(), O_CREAT | O_APPEND | O_WRONLY, 0644));
    if (!fd)
        throw SysError("opening log file %1%", path);

    return std::make_unique<JSONFileLogger>(std::move(fd), includeNixPrefix);
}

void applyJSONLogger()
{
    if (!loggerSettings.jsonLogPath.get().empty()) {
        try {
            std::vector<std::unique_ptr<Logger>> loggers;
            loggers.push_back(makeJSONLogger(std::filesystem::path(loggerSettings.jsonLogPath.get()), false));
            try {
                logger = makeTeeLogger(std::move(logger), std::move(loggers));
            } catch (...) {
                // `logger` is now gone so give up.
                abort();
            }
        } catch (...) {
            ignoreExceptionExceptInterrupt();
        }
    }
}

static Logger::Fields getFields(nlohmann::json & json)
{
    Logger::Fields fields;
    for (auto & f : json) {
        if (f.type() == nlohmann::json::value_t::number_unsigned)
            fields.emplace_back(Logger::Field(f.get<uint64_t>()));
        else if (f.type() == nlohmann::json::value_t::string)
            fields.emplace_back(Logger::Field(f.get<std::string>()));
        else
            throw Error("unsupported JSON type %d", (int) f.type());
    }
    return fields;
}

std::optional<nlohmann::json> parseJSONMessage(const std::string & msg, std::string_view source)
{
    if (!hasPrefix(msg, "@nix "))
        return std::nullopt;
    try {
        return nlohmann::json::parse(std::string(msg, 5));
    } catch (std::exception & e) {
        printError("bad JSON log message from %s: %s", Uncolored(source), e.what());
    }
    return std::nullopt;
}

bool handleJSONLogMessage(
    nlohmann::json & json,
    const Activity & act,
    std::map<ActivityId, Activity> & activities,
    std::string_view source,
    bool trusted)
{
    try {
        std::string action = json["action"];

        if (action == "start") {
            auto type = (ActivityType) json["type"];
            if (trusted || type == actFileTransfer)
                activities.emplace(
                    std::piecewise_construct,
                    std::forward_as_tuple(json["id"]),
                    std::forward_as_tuple(
                        *logger, (Verbosity) json["level"], type, json["text"], getFields(json["fields"]), act.id));
        }

        else if (action == "stop")
            activities.erase((ActivityId) json["id"]);

        else if (action == "result") {
            auto i = activities.find((ActivityId) json["id"]);
            if (i != activities.end())
                i->second.result((ResultType) json["type"], getFields(json["fields"]));
        }

        else if (action == "setPhase") {
            std::string phase = json["phase"];
            act.result(resSetPhase, phase);
        }

        else if (action == "msg") {
            std::string msg = json["msg"];
            logger->log((Verbosity) json["level"], msg);
        }

        return true;
    } catch (const nlohmann::json::exception & e) {
        warn("Unable to handle a JSON message from %s: %s", Uncolored(source), e.what());
        return false;
    }
}

bool handleJSONLogMessage(
    const std::string & msg,
    const Activity & act,
    std::map<ActivityId, Activity> & activities,
    std::string_view source,
    bool trusted)
{
    auto json = parseJSONMessage(msg, source);
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
