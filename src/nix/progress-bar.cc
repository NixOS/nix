#include "progress-bar.hh"
#include "util.hh"
#include "sync.hh"
#include "store-api.hh"
#include "names.hh"

#include <atomic>
#include <map>
#include <thread>

namespace nix {

static std::string getS(const std::vector<Logger::Field> & fields, size_t n)
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
        bool haveUpdate = true;
    };

    Sync<State> state_;

    std::thread updateThread;

    std::condition_variable quitCV, updateCV;

    bool printBuildLogs;
    bool isTTY;

public:

    ProgressBar(bool printBuildLogs, bool isTTY)
        : printBuildLogs(printBuildLogs)
        , isTTY(isTTY)
    {
        state_.lock()->active = isTTY;
        updateThread = std::thread([&]() {
            auto state(state_.lock());
            while (state->active) {
                if (!state->haveUpdate)
                    state.wait(updateCV);
                draw(*state);
                state.wait_for(quitCV, std::chrono::milliseconds(50));
            }
        });
    }

    ~ProgressBar()
    {
        stop();
        updateThread.join();
    }

    void stop()
    {
        auto state(state_.lock());
        if (!state->active) return;
        state->active = false;
        std::string status = getStatus(*state);
        writeToStderr("\r\e[K");
        if (status != "")
            writeToStderr("[" + status + "]\n");
        updateCV.notify_one();
        quitCV.notify_one();
    }

    void log(Verbosity lvl, const FormatOrString & fs) override
    {
        auto state(state_.lock());
        log(*state, lvl, fs.s);
    }

    void log(State & state, Verbosity lvl, const std::string & s)
    {
        if (state.active) {
            writeToStderr("\r\e[K" + filterANSIEscapes(s, !isTTY) + ANSI_NORMAL "\n");
            draw(state);
        } else {
            auto s2 = s + ANSI_NORMAL "\n";
            if (!isTTY) s2 = filterANSIEscapes(s2, true);
            writeToStderr(s2);
        }
    }

    void startActivity(ActivityId act, Verbosity lvl, ActivityType type,
        const std::string & s, const Fields & fields, ActivityId parent) override
    {
        auto state(state_.lock());

        if (lvl <= verbosity && !s.empty())
            log(*state, lvl, s + "...");

        state->activities.emplace_back(ActInfo());
        auto i = std::prev(state->activities.end());
        i->s = s;
        i->type = type;
        i->parent = parent;
        state->its.emplace(act, i);
        state->activitiesByType[type].its.emplace(act, i);

        if (type == actBuild) {
            auto name = storePathToName(getS(fields, 0));
            if (hasSuffix(name, ".drv"))
                name.resize(name.size() - 4);
            i->s = fmt("building " ANSI_BOLD "%s" ANSI_NORMAL, name);
            auto machineName = getS(fields, 1);
            if (machineName != "")
                i->s += fmt(" on " ANSI_BOLD "%s" ANSI_NORMAL, machineName);
            auto curRound = getI(fields, 2);
            auto nrRounds = getI(fields, 3);
            if (nrRounds != 1)
                i->s += fmt(" (round %d/%d)", curRound, nrRounds);
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
                name.resize(name.size() - 4);
            i->s = fmt("post-build " ANSI_BOLD "%s" ANSI_NORMAL, name);
            i->name = DrvName(name).name;
        }

        if (type == actQueryPathInfo) {
            auto name = storePathToName(getS(fields, 0));
            i->s = fmt("querying " ANSI_BOLD "%s" ANSI_NORMAL " on %s", name, getS(fields, 1));
        }

        if ((type == actDownload && hasAncestor(*state, actCopyPath, parent))
            || (type == actDownload && hasAncestor(*state, actQueryPathInfo, parent))
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
            auto lastLine = trim(getS(fields, 0));
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
    }

    void update(State & state)
    {
        state.haveUpdate = true;
        updateCV.notify_one();
    }

    void draw(State & state)
    {
        state.haveUpdate = false;
        if (!state.active) return;

        std::string line;

        std::string status = getStatus(state);
        if (!status.empty()) {
            line += '[';
            line += status;
            line += "]";
        }

        if (!state.activities.empty()) {
            if (!status.empty()) line += " ";
            auto i = state.activities.rbegin();

            while (i != state.activities.rend() && (!i->visible || (i->s.empty() && i->lastLine.empty())))
                ++i;

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

        writeToStderr("\r" + filterANSIEscapes(line, false, width) + "\e[K");
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

        showActivity(actDownload, "%s MiB DL", "%.1f", MiB);

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
};

void startProgressBar(bool printBuildLogs)
{
    logger = new ProgressBar(
        printBuildLogs,
        isatty(STDERR_FILENO) && getEnv("TERM", "dumb") != "dumb");
}

void stopProgressBar()
{
    auto progressBar = dynamic_cast<ProgressBar *>(logger);
    if (progressBar) progressBar->stop();

}

}
