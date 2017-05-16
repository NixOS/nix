#include "progress-bar.hh"
#include "util.hh"
#include "sync.hh"
#include "store-api.hh"

#include <map>
#include <atomic>

#include <sys/ioctl.h>

namespace nix {

class ProgressBar : public Logger
{
private:

    struct ActInfo
    {
        std::string s, s2;
    };

    struct DownloadInfo
    {
        std::string uri;
        uint64_t current = 0;
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
        uint64_t downloadedBytes = 0; // finished downloads
        std::map<Activity::t, DownloadInfo> downloads;
        std::list<ActInfo> activities;
        std::map<Activity::t, std::list<ActInfo>::iterator> its;
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

    void createActivity(State & state, Activity::t activity, const std::string & s)
    {
        state.activities.emplace_back(ActInfo{s});
        state.its.emplace(activity, std::prev(state.activities.end()));
    }

    void deleteActivity(State & state, Activity::t activity)
    {
        auto i = state.its.find(activity);
        if (i != state.its.end()) {
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
            line += i->s;
            if (!i->s2.empty()) {
                line += ": ";
                line += i->s2;
            }
        }

        line += "\e[K";
        writeToStderr(std::string(line, 0, width - 1));
    }

    std::string getStatus(State & state)
    {
        std::string res;

        if (state.failedBuilds) {
            if (!res.empty()) res += ", ";
            res += fmt(ANSI_RED "%d failed" ANSI_NORMAL, state.failedBuilds);
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

        if (!state.downloads.empty() || state.downloadedBytes) {
            if (!res.empty()) res += ", ";
            uint64_t expected = state.downloadedBytes, current = state.downloadedBytes;
            for (auto & i : state.downloads) {
                expected += i.second.expected;
                current += i.second.current;
            }
            res += fmt("%1$.0f/%2$.0f KiB", current / 1024.0, expected / 1024.0);
        }

        return res;
    }

    void event(const Event & ev) override
    {
        if (ev.type == evBuildCreated) {
            auto state(state_.lock());
            state->builds[ev.getI(0)] = ev.getS(1);
            update(*state);
        }

        if (ev.type == evBuildStarted) {
            auto state(state_.lock());
            Activity::t act = ev.getI(0);
            state->runningBuilds.insert(act);
            auto name = storePathToName(state->builds[act]);
            if (hasSuffix(name, ".drv"))
                name.resize(name.size() - 4);
            createActivity(*state, act, fmt("building " ANSI_BOLD "%s" ANSI_NORMAL, name));
            update(*state);
        }

        if (ev.type == evBuildFinished) {
            auto state(state_.lock());
            Activity::t act = ev.getI(0);
            if (ev.getI(1)) {
                if (state->runningBuilds.count(act))
                    state->succeededBuilds++;
            } else
                state->failedBuilds++;
            state->runningBuilds.erase(act);
            state->builds.erase(act);
            deleteActivity(*state, act);
            update(*state);
        }

        if (ev.type == evBuildOutput) {
            auto state(state_.lock());
            Activity::t act = ev.getI(0);
            assert(state->runningBuilds.count(act));
            updateActivity(*state, act, ev.getS(1));
            update(*state);
        }

        if (ev.type == evSubstitutionCreated) {
            auto state(state_.lock());
            state->substitutions[ev.getI(0)] = ev.getS(1);
            update(*state);
        }

        if (ev.type == evSubstitutionStarted) {
            auto state(state_.lock());
            Activity::t act = ev.getI(0);
            state->runningSubstitutions.insert(act);
            auto name = storePathToName(state->substitutions[act]);
            createActivity(*state, act, fmt("fetching " ANSI_BOLD "%s" ANSI_NORMAL, name));
            update(*state);
        }

        if (ev.type == evSubstitutionFinished) {
            auto state(state_.lock());
            Activity::t act = ev.getI(0);
            if (ev.getI(1)) {
                if (state->runningSubstitutions.count(act))
                    state->succeededSubstitutions++;
            }
            state->runningSubstitutions.erase(act);
            state->substitutions.erase(act);
            deleteActivity(*state, act);
            update(*state);
        }

        if (ev.type == evDownloadCreated) {
            auto state(state_.lock());
            Activity::t act = ev.getI(0);
            std::string uri = ev.getS(1);
            state->downloads.emplace(act, DownloadInfo{uri});
            if (state->runningSubstitutions.empty()) // FIXME: hack
                createActivity(*state, act, fmt("downloading " ANSI_BOLD "%s" ANSI_NORMAL "", uri));
            update(*state);
        }

        if (ev.type == evDownloadProgress) {
            auto state(state_.lock());
            Activity::t act = ev.getI(0);
            auto i = state->downloads.find(act);
            assert(i != state->downloads.end());
            i->second.expected = ev.getI(1);
            i->second.current = ev.getI(2);
            update(*state);
        }

        if (ev.type == evDownloadSucceeded) {
            auto state(state_.lock());
            Activity::t act = ev.getI(0);
            auto i = state->downloads.find(act);
            assert(i != state->downloads.end());
            state->downloadedBytes += ev.getI(1);
            state->downloads.erase(i);
            deleteActivity(*state, act);
            update(*state);
        }

        if (ev.type == evDownloadDestroyed) {
            auto state(state_.lock());
            Activity::t act = ev.getI(0);
            auto i = state->downloads.find(act);
            if (i != state->downloads.end()) {
                state->downloads.erase(i);
                deleteActivity(*state, act);
                update(*state);
            }
        }
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
