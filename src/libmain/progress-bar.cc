#include "progress-bar.hh"
#include "util.hh"
#include "sync.hh"
#include "store-api.hh"
#include "names.hh"

#include <atomic>
#include <map>
#include <thread>
#include <iostream>
#include <chrono>
#include <future>

#include <termios.h>
#include <poll.h>

namespace nix {

ProgressBarSettings progressBarSettings;

static GlobalConfig::Register rProgressBarSettings(&progressBarSettings);

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
        std::string s, lastLine;
        std::optional<std::string> phase;
        ActivityType type = actUnknown;
        uint64_t done = 0;
        uint64_t expected = 0;
        uint64_t running = 0;
        uint64_t failed = 0;
        std::map<ActivityType, uint64_t> expectedByType;
        bool visible = true;
        bool ignored = false;
        ActivityId parent;
        std::optional<std::string> name;
        std::optional<std::chrono::time_point<std::chrono::steady_clock>> startTime;
        PathSet buildsRemaining, substitutionsRemaining;
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
        uint64_t active = 0;
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
            if (j.second->ignored) continue;
            stats.done += j.second->done;
            stats.expected += j.second->expected;
            stats.running += j.second->running;
            stats.failed += j.second->failed;
            stats.left += j.second->expected > j.second->done ? j.second->expected - j.second->done : 0;
            stats.active++;
        }

        stats.expected = std::max(stats.expected, act.expected);

        return stats;
    }

    enum StatusLineGroup {
        idHelp,
        idLockFlake,
        idDownload,
        idEvaluate,
        idQueryMissing,
        idCopyPaths,
        idBuilds,
        idVerifyPaths,
        idStatus,
        idQuit,
        idPrompt,
    };

    typedef std::pair<StatusLineGroup, uint16_t> LineId;

    struct State
    {
        std::list<ActInfo> activities;
        std::map<ActivityId, std::list<ActInfo>::iterator> its;

        std::map<ActivityType, ActivitiesByType> activitiesByType;

        uint64_t filesLinked = 0, bytesLinked = 0;

        uint64_t corruptedPaths = 0, untrustedPaths = 0;

        bool active = true;
        bool haveUpdate = true;

        std::map<LineId, std::string> statusLines;

        /* How many lines need to be erased when redrawing. */
        size_t prevStatusLines = 0;

        bool helpShown = false;

        std::optional<std::promise<std::optional<char>>> prompt;
    };

    const bool isTTY;

    Sync<State> state_;

    std::thread updateThread;
    std::thread inputThread;

    std::condition_variable quitCV, updateCV;

    std::optional<struct termios> savedTermAttrs;

    Pipe inputPipe;

    const std::chrono::time_point<std::chrono::steady_clock> startTime = std::chrono::steady_clock::now();

public:

    ProgressBar(bool isTTY)
        : isTTY(isTTY)
        , state_({ .active = isTTY })
    {
        state_.lock()->active = isTTY;

        updateThread = std::thread([&]() {
            auto state(state_.lock());
            while (state->active) {
                #if 0
                if (!state->haveUpdate)
                    state.wait(updateCV);
                #endif
                updateStatusLine(*state);
                draw(*state);
                state.wait_for(quitCV, std::chrono::milliseconds(50));
            }
        });

        if (isTTY) {

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

                    if (c == 3 || c == 4 || c == 'q') {
                        auto state(state_.lock());
                        state->statusLines.insert_or_assign({idQuit, 0}, ANSI_RED "Exiting...");
                        draw(*state);
                        triggerInterrupt();
                    }

                    {
                        auto state(state_.lock());
                        if (state->prompt) {
                            state->prompt->set_value(c != '\n' ? c : std::optional<char>());
                            state->prompt.reset();
                            continue;
                        }
                    }

                    if (c == 'l') {
                        auto state(state_.lock());
                        progressBarSettings.printBuildLogs = !progressBarSettings.printBuildLogs;
                        updateStatusLine(*state);
                        draw(*state,
                            progressBarSettings.printBuildLogs
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
                            size_t n = 0;
                            state->statusLines.insert_or_assign({idHelp, n++}, "");
                            state->statusLines.insert_or_assign({idHelp, n++}, ANSI_BOLD "The following keys are available:");
                            state->statusLines.insert_or_assign({idHelp, n++}, ANSI_BOLD "  'v' to increase verbosity.");
                            state->statusLines.insert_or_assign({idHelp, n++}, ANSI_BOLD "  '-' to decrease verbosity.");
                            state->statusLines.insert_or_assign({idHelp, n++}, ANSI_BOLD "  'l' to show build log output.");
                            state->statusLines.insert_or_assign({idHelp, n++}, ANSI_BOLD "  'r' to show what paths remain to be built/substituted.");
                            state->statusLines.insert_or_assign({idHelp, n++}, ANSI_BOLD "  'h' to hide this help message.");
                            state->statusLines.insert_or_assign({idHelp, n++}, ANSI_BOLD "  'q' to quit.");
                            state->statusLines.insert_or_assign({idHelp, n++}, "");
                        }
                        draw(*state);
                    }
                    if (c == 'r') {
                        auto state(state_.lock());

                        PathSet buildsRemaining, substitutionsRemaining;
                        for (auto & act : state->activities) {
                            for (auto & path : act.buildsRemaining) buildsRemaining.insert(path);
                            for (auto & path : act.substitutionsRemaining) substitutionsRemaining.insert(path);
                        }

                        std::string msg;

                        // FIXME: sort by name?

                        if (!buildsRemaining.empty()) {
                            msg += fmt("\n" ANSI_BOLD "%d derivations remaining to be built:\n" ANSI_NORMAL, buildsRemaining.size());
                            for (auto & path : buildsRemaining)
                                msg += fmt("  • %s\n", path);
                        }

                        if (!substitutionsRemaining.empty()) {
                            msg += fmt("\n" ANSI_BOLD "%d paths remaining to be substituted:\n" ANSI_NORMAL, substitutionsRemaining.size());
                            for (auto & path : substitutionsRemaining)
                                msg += fmt("  • %s\n", path);
                        }

                        if (buildsRemaining.empty() && substitutionsRemaining.empty())
                            msg = "\n" ANSI_BOLD "Nothing left to be built or substituted.";

                        draw(*state, chomp(msg));
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

            if (savedTermAttrs) {
                tcsetattr(STDIN_FILENO, TCSANOW, &*savedTermAttrs);
                savedTermAttrs.reset();
            }
        }

        updateThread.join();
    }

    bool isVerbose() override
    {
        return progressBarSettings.printBuildLogs;
    }

    void log(Verbosity lvl, const FormatOrString & fs) override
    {
        if (lvl > verbosity) return;
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

    void removeStatusLines(State & state, StatusLineGroup id)
    {
        for (auto i = state.statusLines.lower_bound({id, 0});
             i != state.statusLines.end() && i->first.first == id; )
            i = state.statusLines.erase(i);
    }

    void resetHelp(State & state)
    {
        removeStatusLines(state, idHelp);
        state.statusLines.insert_or_assign({idHelp, 0}, "");
        state.statusLines.insert_or_assign({idHelp, 1}, ANSI_BOLD "Type 'h' for help.");
        state.statusLines.insert_or_assign({idHelp, 2}, "");
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
            i->s = fmt(ANSI_BOLD "%s" ANSI_NORMAL, name);
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
                ? ANSI_BOLD "%s" ANSI_NORMAL " from %s"
                : ANSI_BOLD "%s" ANSI_NORMAL " from %s",
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

        if (type == actFileTransfer) {
            i->s = getS(fields, 0);
            if (hasAncestor(*state, actCopyPath, parent)
                || hasAncestor(*state, actQueryPathInfo, parent))
                i->ignored = true;
        }

        if (type == actVerifyPath)
            i->s = getS(fields, 0);

        if (type == actFileTransfer
            || (type == actCopyPath && hasAncestor(*state, actSubstitute, parent)) // FIXME?
            || type == actBuild
            || type == actSubstitute
            || type == actLockFlake
            || type == actQueryMissing
            || type == actVerifyPath)
            i->visible = false;

        if (type == actBuild)
            i->startTime = std::chrono::steady_clock::now();

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

            if (!i->second->ignored) {
                actByType.done += i->second->done;
                actByType.failed += i->second->failed;

                for (auto & j : i->second->expectedByType)
                    state->activitiesByType[j.first].expected -= j.second;
            }

            actByType.its.erase(act);
            state->activities.erase(i->second);
            state->its.erase(i);
        }

        update(*state);
    }

    void result(ActivityId act, ResultType type, const std::vector<Field> & fields) override
    {
        auto state(state_.lock());

        auto i = state->its.find(act);
        assert(i != state->its.end());
        ActInfo & actInfo = *i->second;

        if (type == resFileLinked) {
            state->filesLinked++;
            state->bytesLinked += getI(fields, 0);
            update(*state);
        }

        else if (type == resBuildLogLine || type == resPostBuildLogLine) {
            auto lastLine = chomp(getS(fields, 0));
            if (!lastLine.empty()) {
                i->second->lastLine = lastLine;
                if (progressBarSettings.printBuildLogs) {
                    auto suffix = "> ";
                    if (type == resPostBuildLogLine) {
                        suffix = " (post)> ";
                    }
                    log(*state, lvlInfo, ANSI_FAINT + i->second->name.value_or("unnamed") + suffix + ANSI_NORMAL + lastLine);
                } else
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
            i->second->phase = getS(fields, 0);
            update(*state);
        }

        else if (type == resProgress) {
            if (!actInfo.ignored) {
                actInfo.done = getI(fields, 0);
                actInfo.expected = getI(fields, 1);
                actInfo.running = getI(fields, 2);
                actInfo.failed = getI(fields, 3);
                update(*state);
            }
        }

        else if (type == resSetExpected) {
            if (!actInfo.ignored) {
                auto type = (ActivityType) getI(fields, 0);
                auto & j = actInfo.expectedByType[type];
                state->activitiesByType[type].expected -= j;
                j = getI(fields, 1);
                state->activitiesByType[type].expected += j;
                update(*state);
            }
        }

        else if (type == resExpectBuild)
            actInfo.buildsRemaining.insert(getS(fields, 0));

        else if (type == resUnexpectBuild)
            actInfo.buildsRemaining.erase(getS(fields, 0));

        else if (type == resExpectSubstitution)
            actInfo.substitutionsRemaining.insert(getS(fields, 0));

        else if (type == resUnexpectSubstitution)
            actInfo.substitutionsRemaining.erase(getS(fields, 0));
    }

    void update(State & state)
    {
        state.haveUpdate = true;
        updateCV.notify_one();
    }

    void updateStatusLine(State & state)
    {
        std::string line;

        if (!state.activities.empty()) {
            auto i = state.activities.rbegin();

            while (i != state.activities.rend() && (!i->visible || (i->s.empty() && i->lastLine.empty())))
                ++i;

            if (i != state.activities.rend())
                line += i->s;
        }

        removeStatusLines(state, idStatus);
        if (line != "")
            state.statusLines.insert_or_assign({idStatus, 0}, line);

        std::vector<std::string> spinner =
            //{"←", "↖", "↑", "↗", "→", "↘", "↓", "↙"};
            //{"▁", "▂", "▃", "▄", "▅", "▆", "▇", "█", "▇", "▆", "▅", "▄", "▃", "▁"};
            {"⠁", "⠂", "⠄", "⡀", "⢀", "⠠", "⠐", "⠈"};

        auto now = std::chrono::steady_clock::now();

        auto busyMark = ANSI_BOLD + spinner[(std::chrono::duration_cast<std::chrono::milliseconds>(now - startTime).count() / 200) % spinner.size()];

        auto doneMark = ANSI_GREEN "✓";

        auto failMark = ANSI_RED "✗";

        if (state.activitiesByType.count(actEvaluate)) {
            state.statusLines.insert_or_assign({idEvaluate, 0},
                fmt("%s Evaluate",
                    state.activitiesByType[actEvaluate].its.empty()
                    ? doneMark : busyMark));
            state.statusLines.insert_or_assign({idEvaluate, 1}, "");
        }

        if (state.activitiesByType.count(actLockFlake)) {
            state.statusLines.insert_or_assign({idLockFlake, 0},
                fmt("%s Lock flake inputs",
                    state.activitiesByType[actLockFlake].its.empty()
                    ? doneMark : busyMark));
            state.statusLines.insert_or_assign({idLockFlake, 1}, "");
        }

        if (state.activitiesByType.count(actQueryMissing)) {
            state.statusLines.insert_or_assign({idQueryMissing, 0},
                fmt("%s Query missing paths",
                    state.activitiesByType[actQueryMissing].its.empty()
                    ? doneMark : busyMark));
            state.statusLines.insert_or_assign({idQueryMissing, 1}, "");
        }

        auto renderBar = [](uint64_t done, uint64_t failed, uint64_t running, uint64_t expected)
        {
            expected = std::max(expected, (uint64_t) 1);
            auto pct1 = std::min((double) failed / expected, 1.0);
            auto pct2 = std::min((double) (failed + done) / expected, 1.0);
            auto pct3 = std::min((double) (failed + done + running) / expected, 1.0);
            auto barLength = 70;
            size_t chars1 = barLength * pct1;
            size_t chars2 = barLength * pct2;
            size_t chars3 = barLength * pct3;
            assert(chars1 <= chars2);
            assert(chars2 <= chars3);
            return
                ANSI_RED + repeat("█", chars1) +
                ANSI_GREEN + repeat("█", chars2 - chars1) +
                ANSI_WARNING + repeat("▓", chars3 - chars2) +
                ANSI_NORMAL + repeat("▒", barLength - chars3);
        };

        auto fileTransfer = getActivityStats(state.activitiesByType[actFileTransfer]);

        if (fileTransfer.done || fileTransfer.expected) {
            removeStatusLines(state, idDownload);

            size_t n = 0;
            state.statusLines.insert_or_assign({idDownload, n++},
                fmt("%s Download %.1f / %.1f MiB",
                    fileTransfer.active || fileTransfer.done < fileTransfer.expected
                    ? busyMark
                    : doneMark,
                    fileTransfer.done / MiB, fileTransfer.expected / MiB));

            state.statusLines.insert_or_assign({idDownload, n++},
                fmt("  %s", renderBar(fileTransfer.done, 0, fileTransfer.left, fileTransfer.expected)));

            for (auto & build : state.activitiesByType[actFileTransfer].its) {
                if (build.second->ignored) continue;
                state.statusLines.insert_or_assign({idDownload, n++},
                    fmt(ANSI_BOLD "  ‣ %s", build.second->s));
            }

            state.statusLines.insert_or_assign({idDownload, n++}, "");
        }

        auto copyPath = getActivityStats(state.activitiesByType[actCopyPath]);
        auto copyPaths = getActivityStats(state.activitiesByType[actCopyPaths]);

        if (copyPath.done || copyPath.expected) {
            // FIXME: handle failures

            removeStatusLines(state, idCopyPaths);

            size_t n = 0;
            state.statusLines.insert_or_assign({idCopyPaths, n++},
                fmt("%s Fetch %d / %d store paths, %.1f / %.1f MiB",
                    copyPaths.running || copyPaths.done < copyPaths.expected
                    ? busyMark
                    : doneMark,
                    copyPaths.done, copyPaths.expected,
                    copyPath.done / MiB, copyPath.expected / MiB));

            state.statusLines.insert_or_assign({idCopyPaths, n++},
                fmt("  %s", renderBar(copyPath.done, 0, copyPath.left, copyPath.expected)));

            for (auto & build : state.activitiesByType[actSubstitute].its) {
                state.statusLines.insert_or_assign({idCopyPaths, n++},
                    fmt(ANSI_BOLD "  ‣ %s", build.second->s));
            }

            state.statusLines.insert_or_assign({idCopyPaths, n++}, "");
        }

        auto builds = getActivityStats(state.activitiesByType[actBuilds]);

        if (builds.done || builds.expected) {
            removeStatusLines(state, idBuilds);

            size_t n = 0;
            state.statusLines.insert_or_assign(
                {idBuilds, n++},
                fmt("%s Build %d / %d derivations",
                    builds.failed
                    ? failMark
                    : builds.running || builds.done < builds.expected
                    ? busyMark
                    : doneMark,
                    builds.done, builds.expected)
                + (builds.running ? fmt(", %d running", builds.running) : "")
                + (builds.failed ? fmt(", %d failed", builds.failed) : ""));

            state.statusLines.insert_or_assign({idBuilds, n++},
                fmt("  %s",
                    renderBar(builds.done, builds.failed, builds.running, builds.expected)));

            for (auto & build : state.activitiesByType[actBuild].its) {
                state.statusLines.insert_or_assign({idBuilds, n++},
                    fmt(ANSI_BOLD "  ‣ %s (%d s)%s: %s",
                        build.second->s,
                        std::chrono::duration_cast<std::chrono::seconds>(now - *build.second->startTime).count(),
                        build.second->phase ? fmt(" (%s)", *build.second->phase) : "",
                        build.second->lastLine));
            }

            state.statusLines.insert_or_assign({idBuilds, n++}, "");
        }

        auto verify = getActivityStats(state.activitiesByType[actVerifyPaths]);

        if (verify.done || verify.expected) {
            removeStatusLines(state, idVerifyPaths);

            auto bad = state.corruptedPaths + state.untrustedPaths + verify.failed;

            size_t n = 0;
            state.statusLines.insert_or_assign(
                {idVerifyPaths, n++},
                fmt("%s Verify %d / %d paths",
                    verify.done < verify.expected
                    ? busyMark
                    : bad
                    ? failMark
                    : doneMark,
                    verify.done, verify.expected)
                + (state.corruptedPaths ? fmt(", %d corrupted", state.corruptedPaths) : "")
                + (state.untrustedPaths ? fmt(", %d untrusted", state.untrustedPaths) : "")
                + (verify.failed ? fmt(", %d failed", verify.failed) : "")
                );

            state.statusLines.insert_or_assign({idVerifyPaths, n++},
                fmt("  %s", renderBar(verify.done - bad, bad, verify.running, verify.expected)));

            for (auto & build : state.activitiesByType[actVerifyPath].its) {
                state.statusLines.insert_or_assign({idVerifyPaths, n++},
                    fmt(ANSI_BOLD "  ‣ %s", build.second->s));
            }

            state.statusLines.insert_or_assign({idVerifyPaths, n++}, "");
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

    void writeToStdout(std::string_view s) override
    {
        auto state(state_.lock());

        if (state->active)
            // Note: this assumes that stdout == stderr == a terminal
            draw(*state, s);
        else
            Logger::writeToStdout(s);
    }

    std::optional<char> ask(std::string_view msg) override
    {
        checkInterrupt();

        std::future<std::optional<char>> fut;

        {
            auto state(state_.lock());
            if (!inputThread.joinable()) return {};
            state->statusLines.insert_or_assign({idPrompt, 0}, fmt(ANSI_BOLD "%s " ANSI_NORMAL, msg));
            draw(*state);
            state->prompt = std::promise<std::optional<char>>();
            fut = state->prompt->get_future();
        }

        auto res = fut.get();

        {
            auto state(state_.lock());
            removeStatusLines(*state, idPrompt);
            draw(*state);
        }

        return res;
    }
};

Logger * makeProgressBar()
{
    return new ProgressBar(shouldANSI() && isatty(STDIN_FILENO));
}

void stopProgressBar()
{
    auto progressBar = dynamic_cast<ProgressBar *>(logger);
    if (progressBar) progressBar->stop();

}

}
