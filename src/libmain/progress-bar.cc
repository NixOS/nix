#include "progress-bar.hh"
#include "terminal.hh"
#include "sync.hh"
#include "store-api.hh"
#include "names.hh"

#include <atomic>
#include <map>
#include <thread>
#include <iostream>
#include <chrono>

namespace nix {

static std::string_view getS(const std::vector<Logger::Field> & fields, size_t n)
{
    assert(n < fields.size());
    assert(fields[n].type == Logger::Field::tString);
    return fields[n].s;
}

static uint64_t getI(const std::vector<Logger::Field> & fields, size_t n)
{
    assert(n < fields.size());
    assert(fields[n].type == Logger::Field::tInt);
    return fields[n].i;
}

static std::string_view storePathToName(std::string_view path)
{
    auto base = baseNameOf(path);
    auto i = base.find('-');
    return i == std::string::npos ? base.substr(0, 0) : base.substr(i + 1);
}

class ProgressBar : public Logger
{
private:

    struct ActInfo
    {
        std::string s, lastLine, phase;
        ActivityType type = actUnknown;
        uint64_t done = 0;
        uint64_t expected = 0;
        uint64_t running = 0;
        uint64_t failed = 0;
        std::map<ActivityType, uint64_t> expectedByType;
        bool visible = true;
        ActivityId parent;
        std::optional<std::string> name;
        std::chrono::time_point<std::chrono::steady_clock> startTime;
    };

    struct ActivitiesByType
    {
        std::map<ActivityId, std::list<ActInfo>::iterator> its;
        uint64_t done = 0;
        uint64_t expected = 0;
        uint64_t failed = 0;
    };

    struct State
    {
        std::list<ActInfo> activities;
        std::map<ActivityId, std::list<ActInfo>::iterator> its;

        std::map<ActivityType, ActivitiesByType> activitiesByType;

        uint64_t filesLinked = 0, bytesLinked = 0;

        uint64_t corruptedPaths = 0, untrustedPaths = 0;

        bool active = true;
        bool paused = false;
        bool haveUpdate = true;
    };

    Sync<State> state_;

    std::thread updateThread;

    std::condition_variable quitCV, updateCV;

    bool printBuildLogs = false;
    bool isTTY;

public:

    ProgressBar(bool isTTY)
        : isTTY(isTTY)
    {
        state_.lock()->active = isTTY;
        updateThread = std::thread([&]() {
            auto state(state_.lock());
            auto nextWakeup = std::chrono::milliseconds::max();
            while (state->active) {
                if (!state->haveUpdate)
                    state.wait_for(updateCV, nextWakeup);
                nextWakeup = draw(*state);
                state.wait_for(quitCV, std::chrono::milliseconds(50));
            }
        });
    }

    ~ProgressBar()
    {
        stop();
    }

    /* Called by destructor, can't be overridden */
    void stop() override final
    {
        {
            auto state(state_.lock());
            if (!state->active) return;
            state->active = false;
            writeToStderr("\r\e[K");
            updateCV.notify_one();
            quitCV.notify_one();
        }
        updateThread.join();
    }

    void pause() override {
        auto state (state_.lock());
        state->paused = true;
        if (state->active)
            writeToStderr("\r\e[K");
    }

    void resume() override {
        auto state (state_.lock());
        state->paused = false;
        if (state->active)
            writeToStderr("\r\e[K");
        state->haveUpdate = true;
        updateCV.notify_one();
    }

    bool isVerbose() override
    {
        return printBuildLogs;
    }

    void log(Verbosity lvl, std::string_view s) override
    {
        if (lvl > verbosity) return;
        auto state(state_.lock());
        log(*state, lvl, s);
    }

    void logEI(const ErrorInfo & ei) override
    {
        auto state(state_.lock());

        std::stringstream oss;
        showErrorInfo(oss, ei, loggerSettings.showTrace.get());

        log(*state, ei.level, oss.str());
    }

    void log(State & state, Verbosity lvl, std::string_view s)
    {
        if (state.active) {
            writeToStderr("\r\e[K" + filterANSIEscapes(s, !isTTY) + ANSI_NORMAL "\n");
            draw(state);
        } else {
            writeToStderr(filterANSIEscapes(s, !isTTY) + "\n");
        }
    }

    void startActivity(ActivityId act, Verbosity lvl, ActivityType type,
        const std::string & s, const Fields & fields, ActivityId parent) override
    {
        auto state(state_.lock());

        if (lvl <= verbosity && !s.empty() && type != actBuildWaiting)
            log(*state, lvl, s + "...");

        state->activities.emplace_back(ActInfo {
            .s = s,
            .type = type,
            .parent = parent,
            .startTime = std::chrono::steady_clock::now()
        });
        auto i = std::prev(state->activities.end());
        state->its.emplace(act, i);
        state->activitiesByType[type].its.emplace(act, i);

        if (type == actBuild) {
            std::string name(storePathToName(getS(fields, 0)));
            if (hasSuffix(name, ".drv"))
                name = name.substr(0, name.size() - 4);
            i->s = fmt("building " ANSI_BOLD "%s" ANSI_NORMAL, name);
            auto machineName = getS(fields, 1);
            if (machineName != "")
                i->s += fmt(" on " ANSI_BOLD "%s" ANSI_NORMAL, machineName);

            // Used to be curRound and nrRounds, but the
            // implementation was broken for a long time.
            if (getI(fields, 2) != 1 || getI(fields, 3) != 1) {
                throw Error("log message indicated repeating builds, but this is not currently implemented");
            }
            i->name = DrvName(name).name;
        }

        if (type == actSubstitute) {
            auto name = storePathToName(getS(fields, 0));
            auto sub = getS(fields, 1);
            i->s = fmt(
                hasPrefix(sub, "local")
                ? "copying " ANSI_BOLD "%s" ANSI_NORMAL " from %s"
                : "fetching " ANSI_BOLD "%s" ANSI_NORMAL " from %s",
                name, sub);
        }

        if (type == actPostBuildHook) {
            auto name = storePathToName(getS(fields, 0));
            if (hasSuffix(name, ".drv"))
                name = name.substr(0, name.size() - 4);
            i->s = fmt("post-build " ANSI_BOLD "%s" ANSI_NORMAL, name);
            i->name = DrvName(name).name;
        }

        if (type == actQueryPathInfo) {
            auto name = storePathToName(getS(fields, 0));
            i->s = fmt("querying " ANSI_BOLD "%s" ANSI_NORMAL " on %s", name, getS(fields, 1));
        }

        if ((type == actFileTransfer && hasAncestor(*state, actCopyPath, parent))
            || (type == actFileTransfer && hasAncestor(*state, actQueryPathInfo, parent))
            || (type == actCopyPath && hasAncestor(*state, actSubstitute, parent)))
            i->visible = false;

        update(*state);
    }

    /* Check whether an activity has an ancestore with the specified
       type. */
    bool hasAncestor(State & state, ActivityType type, ActivityId act)
    {
        while (act != 0) {
            auto i = state.its.find(act);
            if (i == state.its.end()) break;
            if (i->second->type == type) return true;
            act = i->second->parent;
        }
        return false;
    }

    void stopActivity(ActivityId act) override
    {
        auto state(state_.lock());

        auto i = state->its.find(act);
        if (i != state->its.end()) {

            auto & actByType = state->activitiesByType[i->second->type];
            actByType.done += i->second->done;
            actByType.failed += i->second->failed;

            for (auto & j : i->second->expectedByType)
                state->activitiesByType[j.first].expected -= j.second;

            actByType.its.erase(act);
            state->activities.erase(i->second);
            state->its.erase(i);
        }

        update(*state);
    }

    void result(ActivityId act, ResultType type, const std::vector<Field> & fields) override
    {
        auto state(state_.lock());

        if (type == resFileLinked) {
            state->filesLinked++;
            state->bytesLinked += getI(fields, 0);
            update(*state);
        }

        else if (type == resBuildLogLine || type == resPostBuildLogLine) {
            auto lastLine = chomp(getS(fields, 0));
            if (!lastLine.empty()) {
                auto i = state->its.find(act);
                assert(i != state->its.end());
                ActInfo info = *i->second;
                if (printBuildLogs) {
                    auto suffix = "> ";
                    if (type == resPostBuildLogLine) {
                        suffix = " (post)> ";
                    }
                    log(*state, lvlInfo, ANSI_FAINT + info.name.value_or("unnamed") + suffix + ANSI_NORMAL + lastLine);
                } else {
                    state->activities.erase(i->second);
                    info.lastLine = lastLine;
                    state->activities.emplace_back(info);
                    i->second = std::prev(state->activities.end());
                    update(*state);
                }
            }
        }

        else if (type == resUntrustedPath) {
            state->untrustedPaths++;
            update(*state);
        }

        else if (type == resCorruptedPath) {
            state->corruptedPaths++;
            update(*state);
        }

        else if (type == resSetPhase) {
            auto i = state->its.find(act);
            assert(i != state->its.end());
            i->second->phase = getS(fields, 0);
            update(*state);
        }

        else if (type == resProgress) {
            auto i = state->its.find(act);
            assert(i != state->its.end());
            ActInfo & actInfo = *i->second;
            actInfo.done = getI(fields, 0);
            actInfo.expected = getI(fields, 1);
            actInfo.running = getI(fields, 2);
            actInfo.failed = getI(fields, 3);
            update(*state);
        }

        else if (type == resSetExpected) {
            auto i = state->its.find(act);
            assert(i != state->its.end());
            ActInfo & actInfo = *i->second;
            auto type = (ActivityType) getI(fields, 0);
            auto & j = actInfo.expectedByType[type];
            state->activitiesByType[type].expected -= j;
            j = getI(fields, 1);
            state->activitiesByType[type].expected += j;
            update(*state);
        }

        else if (type == resFetchStatus) {
            auto i = state->its.find(act);
            assert(i != state->its.end());
            ActInfo & actInfo = *i->second;
            actInfo.lastLine = getS(fields, 0);
            update(*state);
        }
    }

    void update(State & state)
    {
        state.haveUpdate = true;
        updateCV.notify_one();
    }

    std::chrono::milliseconds draw(State & state)
    {
        auto nextWakeup = std::chrono::milliseconds::max();

        state.haveUpdate = false;
        if (state.paused || !state.active) return nextWakeup;

        std::string line;

        std::string status = getStatus(state);
        if (!status.empty()) {
            line += '[';
            line += status;
            line += "]";
        }

        auto now = std::chrono::steady_clock::now();

        if (!state.activities.empty()) {
            if (!status.empty()) line += " ";
            auto i = state.activities.rbegin();

            while (i != state.activities.rend()) {
                if (i->visible && (!i->s.empty() || !i->lastLine.empty())) {
                    /* Don't show activities until some time has
                       passed, to avoid displaying very short
                       activities. */
                    auto delay = std::chrono::milliseconds(10);
                    if (i->startTime + delay < now)
                        break;
                    else
                        nextWakeup = std::min(nextWakeup, std::chrono::duration_cast<std::chrono::milliseconds>(delay - (now - i->startTime)));
                }
                ++i;
            }

            if (i != state.activities.rend()) {
                line += i->s;
                if (!i->phase.empty()) {
                    line += " (";
                    line += i->phase;
                    line += ")";
                }
                if (!i->lastLine.empty()) {
                    if (!i->s.empty()) line += ": ";
                    line += i->lastLine;
                }
            }
        }

        auto width = getWindowSize().second;
        if (width <= 0) width = std::numeric_limits<decltype(width)>::max();

        writeToStderr("\r" + filterANSIEscapes(line, false, width) + ANSI_NORMAL + "\e[K");

        return nextWakeup;
    }

    std::string getStatus(State & state)
    {
        auto MiB = 1024.0 * 1024.0;

        std::string res;

        auto renderActivity = [&](ActivityType type, const std::string & itemFmt, const std::string & numberFmt = "%d", double unit = 1) {
            auto & act = state.activitiesByType[type];
            uint64_t done = act.done, expected = act.done, running = 0, failed = act.failed;
            for (auto & j : act.its) {
                done += j.second->done;
                expected += j.second->expected;
                running += j.second->running;
                failed += j.second->failed;
            }

            expected = std::max(expected, act.expected);

            std::string s;

            if (running || done || expected || failed) {
                if (running)
                    if (expected != 0)
                        s = fmt(ANSI_BLUE + numberFmt + ANSI_NORMAL "/" ANSI_GREEN + numberFmt + ANSI_NORMAL "/" + numberFmt,
                            running / unit, done / unit, expected / unit);
                    else
                        s = fmt(ANSI_BLUE + numberFmt + ANSI_NORMAL "/" ANSI_GREEN + numberFmt + ANSI_NORMAL,
                            running / unit, done / unit);
                else if (expected != done)
                    if (expected != 0)
                        s = fmt(ANSI_GREEN + numberFmt + ANSI_NORMAL "/" + numberFmt,
                            done / unit, expected / unit);
                    else
                        s = fmt(ANSI_GREEN + numberFmt + ANSI_NORMAL, done / unit);
                else
                    s = fmt(done ? ANSI_GREEN + numberFmt + ANSI_NORMAL : numberFmt, done / unit);
                s = fmt(itemFmt, s);

                if (failed)
                    s += fmt(" (" ANSI_RED "%d failed" ANSI_NORMAL ")", failed / unit);
            }

            return s;
        };

        auto showActivity = [&](ActivityType type, const std::string & itemFmt, const std::string & numberFmt = "%d", double unit = 1) {
            auto s = renderActivity(type, itemFmt, numberFmt, unit);
            if (s.empty()) return;
            if (!res.empty()) res += ", ";
            res += s;
        };

        showActivity(actBuilds, "%s built");

        auto s1 = renderActivity(actCopyPaths, "%s copied");
        auto s2 = renderActivity(actCopyPath, "%s MiB", "%.1f", MiB);

        if (!s1.empty() || !s2.empty()) {
            if (!res.empty()) res += ", ";
            if (s1.empty()) res += "0 copied"; else res += s1;
            if (!s2.empty()) { res += " ("; res += s2; res += ')'; }
        }

        showActivity(actFileTransfer, "%s MiB DL", "%.1f", MiB);

        {
            auto s = renderActivity(actOptimiseStore, "%s paths optimised");
            if (s != "") {
                s += fmt(", %.1f MiB / %d inodes freed", state.bytesLinked / MiB, state.filesLinked);
                if (!res.empty()) res += ", ";
                res += s;
            }
        }

        // FIXME: don't show "done" paths in green.
        showActivity(actVerifyPaths, "%s paths verified");

        if (state.corruptedPaths) {
            if (!res.empty()) res += ", ";
            res += fmt(ANSI_RED "%d corrupted" ANSI_NORMAL, state.corruptedPaths);
        }

        if (state.untrustedPaths) {
            if (!res.empty()) res += ", ";
            res += fmt(ANSI_RED "%d untrusted" ANSI_NORMAL, state.untrustedPaths);
        }

        return res;
    }

    void writeToStdout(std::string_view s) override
    {
        auto state(state_.lock());
        if (state->active) {
            std::cerr << "\r\e[K";
            Logger::writeToStdout(s);
            draw(*state);
        } else {
            Logger::writeToStdout(s);
        }
    }

    std::optional<char> ask(std::string_view msg) override
    {
        auto state(state_.lock());
        if (!state->active) return {};
        std::cerr << fmt("\r\e[K%s ", msg);
        auto s = trim(readLine(STDIN_FILENO));
        if (s.size() != 1) return {};
        draw(*state);
        return s[0];
    }

    void setPrintBuildLogs(bool printBuildLogs) override
    {
        this->printBuildLogs = printBuildLogs;
    }
};

Logger * makeProgressBar()
{
    return new ProgressBar(isTTY());
}

void startProgressBar()
{
    logger = makeProgressBar();
}

void stopProgressBar()
{
    auto progressBar = dynamic_cast<ProgressBar *>(logger);
    if (progressBar) progressBar->stop();

}

}
