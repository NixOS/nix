#include "progress-bar.hh"
#include "util.hh"
#include "sync.hh"
#include "store-api.hh"

#include <map>
#include <atomic>

#include <sys/ioctl.h>

#include <iostream>

namespace nix {

class ProgressBar : public Logger
{
private:

    struct ActInfo
    {
        std::string s, s2;
        ActivityType type = actUnknown;
        uint64_t done = 0;
        uint64_t expected = 0;
        std::map<ActivityType, uint64_t> expectedByType;
    };

    struct CopyInfo
    {
        uint64_t expected = 0;
        uint64_t copied = 0;
        uint64_t done = 0;
    };

    struct ActivitiesByType
    {
        std::map<Activity::t, std::list<ActInfo>::iterator> its;
        uint64_t done = 0;
        uint64_t expected = 0;
    };

    struct State
    {
        std::map<Activity::t, Path> builds;
        std::set<Activity::t> runningBuilds;
        uint64_t succeededBuilds = 0;
        uint64_t failedBuilds = 0;

        std::map<Activity::t, Path> substitutions;
        std::set<Activity::t> runningSubstitutions;
        uint64_t succeededSubstitutions = 0;

        std::map<Activity::t, CopyInfo> runningCopies;

        std::list<ActInfo> activities;
        std::map<Activity::t, std::list<ActInfo>::iterator> its;

        std::map<ActivityType, ActivitiesByType> activitiesByType;
    };

    Sync<State> state_;

    int width = 0;

public:

    ProgressBar()
    {
        struct winsize ws;
        if (ioctl(1, TIOCGWINSZ, &ws) == 0)
            width = ws.ws_col;
    }

    ~ProgressBar()
    {
        auto state(state_.lock());
        writeToStderr("\r\e[K");
    }

    void log(Verbosity lvl, const FormatOrString & fs) override
    {
        auto state(state_.lock());
        log(*state, lvl, fs.s);
    }

    void log(State & state, Verbosity lvl, const std::string & s)
    {
        writeToStderr("\r\e[K" + s + "\n");
        update(state);
    }

    void createActivity(State & state, Activity::t activity, const std::string & s, ActivityType type = actUnknown)
    {
        state.activities.emplace_back(ActInfo{s, "", type});
        auto i = std::prev(state.activities.end());
        state.its.emplace(activity, i);
        state.activitiesByType[type].its.emplace(activity, i);
    }

    void deleteActivity(State & state, Activity::t activity)
    {
        auto i = state.its.find(activity);
        if (i != state.its.end()) {
            auto & act = state.activitiesByType[i->second->type];
            act.done += i->second->done;

            for (auto & j : i->second->expectedByType)
                state.activitiesByType[j.first].expected -= j.second;

            act.its.erase(activity);
            state.activities.erase(i->second);
            state.its.erase(i);
        }
    }

    void updateActivity(State & state, Activity::t activity, const std::string & s2)
    {
        auto i = state.its.find(activity);
        assert(i != state.its.end());
        ActInfo info = *i->second;
        state.activities.erase(i->second);
        info.s2 = s2;
        state.activities.emplace_back(info);
        i->second = std::prev(state.activities.end());
    }

    void update()
    {
        auto state(state_.lock());
        update(*state);
    }

    void update(State & state)
    {
        std::string line = "\r";

        std::string status = getStatus(state);
        if (!status.empty()) {
            line += '[';
            line += status;
            line += "]";
        }

        if (!state.activities.empty()) {
            if (!status.empty()) line += " ";
            auto i = state.activities.rbegin();

            while (i != state.activities.rend() && i->s.empty() && i->s2.empty())
                ++i;

            if (i != state.activities.rend()) {
                line += i->s;
                if (!i->s2.empty()) {
                    line += ": ";
                    line += i->s2;
                }
            }
        }

        line += "\e[K";
        writeToStderr(std::string(line, 0, width - 1));
    }

    std::string getStatus(State & state)
    {
        auto MiB = 1024.0 * 1024.0;

        std::string res;

        auto add = [&](const std::string & s) {
            if (!res.empty()) res += ", ";
            res += s;
        };

        if (state.failedBuilds) {
            add(fmt(ANSI_RED "%d failed" ANSI_NORMAL, state.failedBuilds));
        }

        if (!state.builds.empty() || state.succeededBuilds)
        {
            if (!res.empty()) res += ", ";
            if (!state.runningBuilds.empty())
                res += fmt(ANSI_BLUE "%d" "/" ANSI_NORMAL, state.runningBuilds.size());
            res += fmt(ANSI_GREEN "%d/%d built" ANSI_NORMAL,
                state.succeededBuilds, state.succeededBuilds + state.builds.size());
        }

        if (!state.substitutions.empty() || state.succeededSubstitutions) {
            if (!res.empty()) res += ", ";
            if (!state.runningSubstitutions.empty())
                res += fmt(ANSI_BLUE "%d" "/" ANSI_NORMAL, state.runningSubstitutions.size());
            res += fmt(ANSI_GREEN "%d/%d fetched" ANSI_NORMAL,
                state.succeededSubstitutions,
                state.succeededSubstitutions + state.substitutions.size());
        }

        if (!state.runningCopies.empty()) {
            uint64_t copied = 0, expected = 0;
            for (auto & i : state.runningCopies) {
                copied += i.second.copied;
                expected += i.second.expected - (i.second.done - i.second.copied);
            }
            add(fmt("%d/%d copied", copied, expected));
        }

        auto showActivity = [&](ActivityType type, const std::string & f) {
            auto & act = state.activitiesByType[type];
            uint64_t done = act.done, expected = act.done;
            for (auto & j : act.its) {
                done += j.second->done;
                expected += j.second->expected;
            }

            expected = std::max(expected, act.expected);

            if (done || expected)
                add(fmt(f, done / MiB, expected / MiB));
        };

        showActivity(actDownload, "%1$.1f/%2$.1f MiB DL");
        showActivity(actCopyPath, "%1$.1f/%2$.1f MiB copied");

        return res;
    }

    void event(const Event & ev) override
    {
        auto state(state_.lock());

        if (ev.type == evStartActivity) {
            Activity::t act = ev.getI(0);
            createActivity(*state, act, ev.getS(2), (ActivityType) ev.getI(1));
        }

        if (ev.type == evStopActivity) {
            Activity::t act = ev.getI(0);
            deleteActivity(*state, act);
        }

        if (ev.type == evProgress) {
            auto i = state->its.find(ev.getI(0));
            assert(i != state->its.end());
            ActInfo & actInfo = *i->second;
            actInfo.done = ev.getI(1);
            actInfo.expected = ev.getI(2);
        }

        if (ev.type == evSetExpected) {
            auto i = state->its.find(ev.getI(0));
            assert(i != state->its.end());
            ActInfo & actInfo = *i->second;
            auto type = (ActivityType) ev.getI(1);
            auto & j = actInfo.expectedByType[type];
            state->activitiesByType[type].expected -= j;
            j = ev.getI(2);
            state->activitiesByType[type].expected += j;
        }

        if (ev.type == evBuildCreated) {
            state->builds[ev.getI(0)] = ev.getS(1);
        }

        if (ev.type == evBuildStarted) {
            Activity::t act = ev.getI(0);
            state->runningBuilds.insert(act);
            auto name = storePathToName(state->builds[act]);
            if (hasSuffix(name, ".drv"))
                name.resize(name.size() - 4);
            createActivity(*state, act, fmt("building " ANSI_BOLD "%s" ANSI_NORMAL, name));
        }

        if (ev.type == evBuildFinished) {
            Activity::t act = ev.getI(0);
            if (ev.getI(1)) {
                if (state->runningBuilds.count(act))
                    state->succeededBuilds++;
            } else
                state->failedBuilds++;
            state->runningBuilds.erase(act);
            state->builds.erase(act);
            deleteActivity(*state, act);
        }

        if (ev.type == evBuildOutput) {
            Activity::t act = ev.getI(0);
            assert(state->runningBuilds.count(act));
            updateActivity(*state, act, ev.getS(1));
        }

        if (ev.type == evSubstitutionCreated) {
            state->substitutions[ev.getI(0)] = ev.getS(1);
        }

        if (ev.type == evSubstitutionStarted) {
            Activity::t act = ev.getI(0);
            state->runningSubstitutions.insert(act);
            auto name = storePathToName(state->substitutions[act]);
            createActivity(*state, act, fmt("fetching " ANSI_BOLD "%s" ANSI_NORMAL, name));
        }

        if (ev.type == evSubstitutionFinished) {
            Activity::t act = ev.getI(0);
            if (ev.getI(1)) {
                if (state->runningSubstitutions.count(act))
                    state->succeededSubstitutions++;
            }
            state->runningSubstitutions.erase(act);
            state->substitutions.erase(act);
            deleteActivity(*state, act);
        }

        if (ev.type == evCopyProgress) {
            Activity::t act = ev.getI(0);
            auto & i = state->runningCopies[act];
            i.expected = ev.getI(1);
            i.copied = ev.getI(2);
            i.done = ev.getI(3);
        }

        update(*state);
    }
};

StartProgressBar::StartProgressBar()
{
    if (isatty(STDERR_FILENO)) {
        prev = logger;
        logger = new ProgressBar();
    }
}

StartProgressBar::~StartProgressBar()
{
    if (prev) {
        auto bar = logger;
        logger = prev;
        delete bar;
    }
}

}
