#include "logging.hh"
#include "util.hh"
#include "config.hh"

#include <atomic>
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

Logger * logger = makeSimpleLogger(true);

void Logger::warn(const std::string & msg)
{
    log(lvlWarn, ANSI_YELLOW "warning:" ANSI_NORMAL " " + msg);
}

void Logger::writeToStdout(std::string_view s)
{
    std::cout << s << "\n";
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
        tty = isatty(STDERR_FILENO);
    }

    bool isVerbose() override {
        return printBuildLogs;
    }

    void log(Verbosity lvl, const FormatOrString & fs) override
    {
        if (lvl > verbosity) return;

        std::string prefix;

        if (systemd) {
            char c;
            switch (lvl) {
            case lvlError: c = '3'; break;
            case lvlWarn: c = '4'; break;
            case lvlInfo: c = '5'; break;
            case lvlTalkative: case lvlChatty: c = '6'; break;
            default: c = '7';
            }
            prefix = std::string("<") + c + ">";
        }

        writeToStderr(prefix + filterANSIEscapes(fs.s, !tty) + "\n");
    }

    void logEI(const ErrorInfo & ei) override
    {
        std::stringstream oss;
        showErrorInfo(oss, ei, loggerSettings.showTrace.get());

        log(ei.level, oss.str());
    }

    void startActivity(ActivityId act, Verbosity lvl, ActivityType type,
        const std::string & s, const Fields & fields, ActivityId parent)
    override
    {
        if (lvl <= verbosity && !s.empty())
            log(lvl, s + "...");
    }

    void result(ActivityId act, ResultType type, const Fields & fields) override
    {
        if (type == resBuildLogLine && printBuildLogs) {
            auto lastLine = fields[0].s;
            printError(lastLine);
        }
        else if (type == resPostBuildLogLine && printBuildLogs) {
            auto lastLine = fields[0].s;
            printError("post-build-hook: " + lastLine);
        }
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

Logger * makeSimpleLogger(bool printBuildLogs)
{
    return new SimpleLogger(printBuildLogs);
}

std::atomic<uint64_t> nextId{(uint64_t) getpid() << 32};

Activity::Activity(Logger & logger, Verbosity lvl, ActivityType type,
    const std::string & s, const Logger::Fields & fields, ActivityId parent)
    : logger(logger), id(nextId++)
{
    logger.startActivity(id, lvl, type, s, fields, parent);
}

struct JSONLogger : Logger {
    Logger & prevLogger;

    JSONLogger(Logger & prevLogger) : prevLogger(prevLogger) { }

    bool isVerbose() override {
        return true;
    }

    void addFields(nlohmann::json & json, const Fields & fields)
    {
        if (fields.empty()) return;
        auto & arr = json["fields"] = nlohmann::json::array();
        for (auto & f : fields)
            if (f.type == Logger::Field::tInt)
                arr.push_back(f.i);
            else if (f.type == Logger::Field::tString)
                arr.push_back(f.s);
            else
                abort();
    }

    void write(const nlohmann::json & json)
    {
        prevLogger.log(lvlError, "@nix " + json.dump());
    }

    void log(Verbosity lvl, const FormatOrString & fs) override
    {
        nlohmann::json json;
        json["action"] = "msg";
        json["level"] = lvl;
        json["msg"] = fs.s;
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
        json["raw_msg"] = ei.hint->str();

        if (ei.errPos.has_value() && (*ei.errPos)) {
            json["line"] = ei.errPos->line;
            json["column"] = ei.errPos->column;
            json["file"] = ei.errPos->file;
        } else {
            json["line"] = nullptr;
            json["column"] = nullptr;
            json["file"] = nullptr;
        }

        if (loggerSettings.showTrace.get() && !ei.traces.empty()) {
            nlohmann::json traces = nlohmann::json::array();
            for (auto iter = ei.traces.rbegin(); iter != ei.traces.rend(); ++iter) {
                nlohmann::json stackFrame;
                stackFrame["raw_msg"] = iter->hint.str();
                if (iter->pos.has_value() && (*iter->pos)) {
                    stackFrame["line"] = iter->pos->line;
                    stackFrame["column"] = iter->pos->column;
                    stackFrame["file"] = iter->pos->file;
                }
                traces.push_back(stackFrame);
            }

            json["trace"] = traces;
        }

        write(json);
    }

    void startActivity(ActivityId act, Verbosity lvl, ActivityType type,
        const std::string & s, const Fields & fields, ActivityId parent) override
    {
        nlohmann::json json;
        json["action"] = "start";
        json["id"] = act;
        json["level"] = lvl;
        json["type"] = type;
        json["text"] = s;
        addFields(json, fields);
        // FIXME: handle parent
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

Logger * makeJSONLogger(Logger & prevLogger)
{
    return new JSONLogger(prevLogger);
}

static Logger::Fields getFields(nlohmann::json & json)
{
    Logger::Fields fields;
    for (auto & f : json) {
        if (f.type() == nlohmann::json::value_t::number_unsigned)
            fields.emplace_back(Logger::Field(f.get<uint64_t>()));
        else if (f.type() == nlohmann::json::value_t::string)
            fields.emplace_back(Logger::Field(f.get<std::string>()));
        else throw Error("unsupported JSON type %d", (int) f.type());
    }
    return fields;
}

bool handleJSONLogMessage(const std::string & msg,
    const Activity & act, std::map<ActivityId, Activity> & activities, bool trusted)
{
    if (!hasPrefix(msg, "@nix ")) return false;

    try {
        auto json = nlohmann::json::parse(std::string(msg, 5));

        std::string action = json["action"];

        if (action == "start") {
            auto type = (ActivityType) json["type"];
            if (trusted || type == actFileTransfer)
                activities.emplace(std::piecewise_construct,
                    std::forward_as_tuple(json["id"]),
                    std::forward_as_tuple(*logger, (Verbosity) json["level"], type,
                        json["text"], getFields(json["fields"]), act.id));
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

    } catch (std::exception & e) {
        logError({
            .name = "Json log message",
            .hint = hintfmt("bad log message from builder: %s", e.what())
        });
    }

    return true;
}

Activity::~Activity()
{
    try {
        logger.stopActivity(id);
    } catch (...) {
        ignoreException();
    }
}

}
