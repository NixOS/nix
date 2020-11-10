#include "progress-bar.hh"
#include "util.hh"
#include "sync.hh"
#include "store-api.hh"
#include "names.hh"

#include <atomic>
#include <map>
#include <thread>
#include <iostream>

#include <termios.h>
#include <poll.h>

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

static std::string_view storePathToName(std::string_view path)
{
    auto base = baseNameOf(path);
    auto i = base.find('-');
    return i == std::string::npos ? base.substr(0, 0) : base.substr(i + 1);
}

std::string repeat(std::string_view s, size_t n)
{
    std::string res;
    for (size_t i = 0; i < n; ++i)
        res += s;
    return res;
}

auto MiB = 1024.0 * 1024.0;

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

    struct ActivityStats
    {
        uint64_t done = 0;
        uint64_t expected = 0;
        uint64_t running = 0;
        uint64_t failed = 0;
        uint64_t left = 0;
    };

    ActivityStats getActivityStats(ActivitiesByType & act)
    {
        ActivityStats stats {
            .done = act.done,
            .expected = act.done,
            .running = 0,
            .failed = act.failed
        };

        for (auto & j : act.its) {
            stats.done += j.second->done;
            stats.expected += j.second->expected;
            stats.running += j.second->running;
            stats.failed += j.second->failed;
            stats.left += j.second->expected - j.second->done;
        }

        stats.expected = std::max(stats.expected, act.expected);

        return stats;
    }

    typedef unsigned int LineId;

    struct State
    {
        std::list<ActInfo> activities;
        std::map<ActivityId, std::list<ActInfo>::iterator> its;

        std::map<ActivityType, ActivitiesByType> activitiesByType;

        uint64_t filesLinked = 0, bytesLinked = 0;

        uint64_t corruptedPaths = 0, untrustedPaths = 0;

        bool active = true;
        bool haveUpdate = true;

        bool printBuildLogs;

        std::map<LineId, std::string> statusLines;

        /* How many lines need to be erased when redrawing. */
        size_t prevStatusLines = 0;

        bool helpShown = false;
    };

    bool isTTY;

    Sync<State> state_;

    std::thread updateThread;
    std::thread inputThread;

    std::condition_variable quitCV, updateCV;

    std::optional<struct termios> savedTermAttrs;

    Pipe inputPipe;

public:

    void resetHelp(State & state)
    {
        for (LineId i = 0; i <= 6; i++)
            state.statusLines.erase(i);
        state.statusLines.insert_or_assign(0, "");
        state.statusLines.insert_or_assign(1, ANSI_BOLD "Type 'h' for help.");
        state.statusLines.insert_or_assign(2, "");
    }

    ProgressBar(bool printBuildLogs, bool isTTY)
        : isTTY(isTTY)
        , state_({ .active = isTTY, .printBuildLogs = printBuildLogs })
    {
        state_.lock()->active = isTTY;

        updateThread = std::thread([&]() {
            auto state(state_.lock());
            while (state->active) {
                if (!state->haveUpdate)
                    state.wait(updateCV);
                updateStatusLine(*state);
                draw(*state);
                state.wait_for(quitCV, std::chrono::milliseconds(50));
            }

            if (savedTermAttrs) {
                tcsetattr(STDIN_FILENO, TCSANOW, &*savedTermAttrs);
                savedTermAttrs.reset();
            }
        });

        if (isatty(STDIN_FILENO) && isatty(STDOUT_FILENO) && isatty(STDERR_FILENO)) {

            struct termios term;
            if (tcgetattr(STDIN_FILENO, &term))
                throw SysError("getting terminal attributes");

            savedTermAttrs = term;

            cfmakeraw(&term);

            if (tcsetattr(STDIN_FILENO, TCSANOW, &term))
                throw SysError("putting terminal into raw mode");

            inputPipe.create();

            inputThread = std::thread([this]() {
                // FIXME: exceptions

                struct pollfd fds[2];
                fds[0] = { .fd = STDIN_FILENO, .events = POLLIN, .revents = 0 };
                fds[1] = { .fd = inputPipe.readSide.get(), .events = POLLIN, .revents = 0 };

                while (true) {
                    if (poll(fds, 2, -1) != 1) {
                        if (errno == EINTR) continue;
                        assert(false);
                    }

                    if (fds[1].revents & POLLIN) break;

                    assert(fds[0].revents & POLLIN);

                    char c;
                    auto n = read(STDIN_FILENO, &c, 1);
                    if (n == 0) break;
                    if (n == -1) {
                        if (errno == EINTR) continue;
                        break;
                    }
                    c = std::tolower(c);

                    if (c == 3 || c == 'q') {
                        triggerInterrupt();
                        break;
                    }
                    if (c == 'l') {
                        auto state(state_.lock());
                        state->printBuildLogs = !state->printBuildLogs;
                        updateStatusLine(*state);
                        draw(*state,
                            state->printBuildLogs
                            ? ANSI_BOLD "Enabling build logs."
                            : ANSI_BOLD "Disabling build logs.");
                    }
                    if (c == '+' || c == '=' || c == 'v') {
                        auto state(state_.lock());
                        verbosity = (Verbosity) (verbosity + 1);;
                        log(*state, lvlError, ANSI_BOLD "Increasing verbosity...");
                    }
                    if (c == '-') {
                        auto state(state_.lock());
                        verbosity = verbosity > lvlError ? (Verbosity) (verbosity - 1) : lvlError;
                        log(*state, lvlError, ANSI_BOLD "Decreasing verbosity...");
                    }
                    if (c == 'h' || c == '?') {
                        auto state(state_.lock());
                        if (state->helpShown) {
                            state->helpShown = false;
                            resetHelp(*state);
                        } else {
                            state->helpShown = true;
                            state->statusLines.insert_or_assign(0, "");
                            state->statusLines.insert_or_assign(1, ANSI_BOLD "The following keys are available:");
                            state->statusLines.insert_or_assign(2, ANSI_BOLD "  'v' to increase verbosity.");
                            state->statusLines.insert_or_assign(3, ANSI_BOLD "  '-' to decrease verbosity.");
                            state->statusLines.insert_or_assign(4, ANSI_BOLD "  'l' to show build log output.");
                            state->statusLines.insert_or_assign(5, ANSI_BOLD "  'q' to quit.");
                            state->statusLines.insert_or_assign(6, "");
                        }
                        draw(*state);
                    }
                }
            });

            resetHelp(*state_.lock());
        }
    }

    ~ProgressBar()
    {
        stop();
    }

    void stop() override
    {
        if (inputThread.joinable()) {
            assert(inputPipe.writeSide);
            writeFull(inputPipe.writeSide.get(), "x", false);
            inputThread.join();
        }

        {
            auto state(state_.lock());
            if (!state->active) return;
            state->statusLines.clear();
            draw(*state);
            state->active = false;
            updateCV.notify_one();
            quitCV.notify_one();
        }

        updateThread.join();
    }

    bool isVerbose() override
    {
        return state_.lock()->printBuildLogs;
    }

    void log(Verbosity lvl, const FormatOrString & fs) override
    {
        auto state(state_.lock());
        log(*state, lvl, fs.s);
    }

    void logEI(const ErrorInfo &ei) override
    {
        auto state(state_.lock());

        std::stringstream oss;
        showErrorInfo(oss, ei, loggerSettings.showTrace.get());

        log(*state, ei.level, oss.str());
    }

    void log(State & state, Verbosity lvl, const std::string & s)
    {
        if (state.active) {
            draw(state, filterANSIEscapes(s, !isTTY));
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

        if (lvl <= verbosity && !s.empty() && type != actBuildWaiting)
            log(*state, lvl, s + "...");

        state->activities.emplace_back(ActInfo());
        auto i = std::prev(state->activities.end());
        i->s = s;
        i->type = type;
        i->parent = parent;
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
                if (state->printBuildLogs) {
                    auto suffix = "> ";
                    if (type == resPostBuildLogLine) {
                        suffix = " (post)> ";
                    }
                    log(*state, lvlInfo, ANSI_FAINT + info.name.value_or("unnamed") + suffix + ANSI_NORMAL + lastLine);
                }
                state->activities.erase(i->second);
                info.lastLine = lastLine;
                state->activities.emplace_back(info);
                i->second = std::prev(state->activities.end());
                if (!state->printBuildLogs)
                    update(*state);
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

    void updateStatusLine(State & state)
    {
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
                if (!state.printBuildLogs && !i->lastLine.empty()) {
                    if (!i->s.empty()) line += ": ";
                    line += i->lastLine;
                }
            }
        }

        if (line.empty())
            state.statusLines.erase(100);
        else
            state.statusLines.insert_or_assign(100, line);

        auto renderBar = [](uint64_t done, uint64_t running, uint64_t expected)
        {
            expected = std::max(expected, (uint64_t) 1);
            auto pct1 = std::min((double) done / expected, 1.0);
            auto pct2 = std::min((double) (done + running) / expected, 1.0);
            auto barLength = 60;
            size_t chars1 = barLength * pct1;
            size_t chars2 = barLength * pct2;
            return
                ANSI_GREEN + repeat("█", chars1) +
                ANSI_YELLOW + repeat("▓", chars2 - chars1) +
                ANSI_NORMAL + repeat("▒", barLength - chars2);
        };

        auto copyPath = getActivityStats(state.activitiesByType[actCopyPath]);
        auto copyPaths = getActivityStats(state.activitiesByType[actCopyPaths]);

        if (copyPath.done || copyPath.expected) {
            state.statusLines.insert_or_assign(50,
                fmt(ANSI_BOLD "•" ANSI_NORMAL " %s " ANSI_BOLD "Fetched" ANSI_NORMAL " %d / %d paths, %.1f / %.1f MiB %d",
                    renderBar(copyPath.done, copyPath.left, copyPath.expected),
                    copyPaths.done, copyPaths.expected,
                    copyPath.done / MiB, copyPath.expected / MiB, copyPath.left));
            state.statusLines.insert_or_assign(51, "");
        }

        auto builds = getActivityStats(state.activitiesByType[actBuilds]);

        if (builds.done || builds.expected) {
            state.statusLines.insert_or_assign(50,
                fmt(ANSI_BOLD "•" ANSI_NORMAL " %s " ANSI_BOLD "Built" ANSI_NORMAL " %d / %d derivations",
                    renderBar(builds.done, builds.running, builds.expected),
                    builds.done, builds.expected));
            state.statusLines.insert_or_assign(51, "");
        }

    }

    void draw(State & state, std::optional<std::string_view> msg = {})
    {
        state.haveUpdate = false;
        if (!state.active) return;

        auto width = getWindowSize().second;
        if (width <= 0) width = std::numeric_limits<decltype(width)>::max();

        std::string s;

        for (size_t i = 1; i < state.prevStatusLines; ++i)
            s += "\r\e[K\e[A";

        s += "\r\e[K";

        if (msg) {
            s += replaceStrings(*msg, "\n", "\r\n");
            s += ANSI_NORMAL "\e[K\n\r";
        }

        for (const auto & [n, i] : enumerate(state.statusLines)) {
            s += filterANSIEscapes(i.second, false, width) + ANSI_NORMAL + "\e[K";
            if (n + 1 < state.statusLines.size()) s += "\r\n";
        }

        writeToStderr(s);

        state.prevStatusLines = state.statusLines.size();
    }

    std::string getStatus(State & state)
    {
        std::string res;

        auto renderActivity = [&](ActivityType type, const std::string & itemFmt, const std::string & numberFmt = "%d", double unit = 1) {
            auto stats = getActivityStats(state.activitiesByType[type]);

            std::string s;

            if (stats.running || stats.done || stats.expected || stats.failed) {
                if (stats.running)
                    if (stats.expected != 0)
                        s = fmt(ANSI_BLUE + numberFmt + ANSI_NORMAL "/" ANSI_GREEN + numberFmt + ANSI_NORMAL "/" + numberFmt,
                            stats.running / unit, stats.done / unit, stats.expected / unit);
                    else
                        s = fmt(ANSI_BLUE + numberFmt + ANSI_NORMAL "/" ANSI_GREEN + numberFmt + ANSI_NORMAL,
                            stats.running / unit, stats.done / unit);
                else if (stats.expected != stats.done)
                    if (stats.expected != 0)
                        s = fmt(ANSI_GREEN + numberFmt + ANSI_NORMAL "/" + numberFmt,
                            stats.done / unit, stats.expected / unit);
                    else
                        s = fmt(ANSI_GREEN + numberFmt + ANSI_NORMAL, stats.done / unit);
                else
                    s = fmt(stats.done ? ANSI_GREEN + numberFmt + ANSI_NORMAL : numberFmt, stats.done / unit);
                s = fmt(itemFmt, s);

                if (stats.failed)
                    s += fmt(" (" ANSI_RED "%d failed" ANSI_NORMAL ")", stats.failed / unit);
            }

            return s;
        };

        auto showActivity = [&](ActivityType type, const std::string & itemFmt, const std::string & numberFmt = "%d", double unit = 1) {
            auto s = renderActivity(type, itemFmt, numberFmt, unit);
            if (s.empty()) return;
            if (!res.empty()) res += ", ";
            res += s;
        };

        #if 0
        showActivity(actBuilds, "%s built");

        auto s1 = renderActivity(actCopyPaths, "%s copied");
        auto s2 = renderActivity(actCopyPath, "%s MiB", "%.1f", MiB);

        if (!s1.empty() || !s2.empty()) {
            if (!res.empty()) res += ", ";
            if (s1.empty()) res += "0 copied"; else res += s1;
            if (!s2.empty()) { res += " ("; res += s2; res += ')'; }
        }

        showActivity(actFileTransfer, "%s MiB DL", "%.1f", MiB);
        #endif

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
            draw(*state, s);
        } else {
            Logger::writeToStdout(s);
        }
    }

    std::optional<char> ask(std::string_view msg) override
    {
        auto state(state_.lock());
        if (!state->active || !isatty(STDIN_FILENO)) return {};
        std::cerr << fmt("\r\e[K%s ", msg);
        auto s = trim(readLine(STDIN_FILENO));
        if (s.size() != 1) return {};
        draw(*state);
        return s[0];
    }
};

Logger * makeProgressBar(bool printBuildLogs)
{
    return new ProgressBar(
        printBuildLogs,
        isatty(STDERR_FILENO) && getEnv("TERM").value_or("dumb") != "dumb"
    );
}

void startProgressBar(bool printBuildLogs)
{
    logger = makeProgressBar(printBuildLogs);
}

void stopProgressBar()
{
    auto progressBar = dynamic_cast<ProgressBar *>(logger);
    if (progressBar) progressBar->stop();

}

}
