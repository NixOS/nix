#include "progress-bar.hh"
#include "util.hh"
#include "sync.hh"

#include <map>

namespace nix {

class ProgressBar : public Logger
{
private:

    struct ActInfo
    {
        Activity * activity;
        Verbosity lvl;
        std::string s;
    };

    struct Progress
    {
        uint64_t expected = 0, progress = 0;
    };

    struct State
    {
        std::list<ActInfo> activities;
        std::map<Activity *, std::list<ActInfo>::iterator> its;
        std::map<std::string, Progress> progress;
    };

    Sync<State> state_;

public:

    ~ProgressBar()
    {
        auto state(state_.lock());
        assert(state->activities.empty());
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

    void startActivity(Activity & activity, Verbosity lvl, const FormatOrString & fs) override
    {
        if (lvl > verbosity) return;
        auto state(state_.lock());
        state->activities.emplace_back(ActInfo{&activity, lvl, fs.s});
        state->its.emplace(&activity, std::prev(state->activities.end()));
        update(*state);
    }

    void stopActivity(Activity & activity) override
    {
        auto state(state_.lock());
        auto i = state->its.find(&activity);
        if (i == state->its.end()) return;
        state->activities.erase(i->second);
        state->its.erase(i);
        update(*state);
    }

    void setExpected(const std::string & label, uint64_t value) override
    {
        auto state(state_.lock());
        state->progress[label].expected = value;
    }

    void setProgress(const std::string & label, uint64_t value) override
    {
        auto state(state_.lock());
        state->progress[label].progress = value;
    }

    void incExpected(const std::string & label, uint64_t value) override
    {
        auto state(state_.lock());
        state->progress[label].expected += value;
    }

    void incProgress(const std::string & label, uint64_t value)
    {
        auto state(state_.lock());
        state->progress[label].progress += value;
    }

    void update()
    {
        auto state(state_.lock());
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
            line += state.activities.rbegin()->s;
        }

        line += "\e[K";
        writeToStderr(line);
    }

    std::string getStatus(State & state)
    {
        std::string res;
        for (auto & p : state.progress)
            if (p.second.expected || p.second.progress) {
                if (!res.empty()) res += ", ";
                res += std::to_string(p.second.progress);
                if (p.second.expected) {
                    res += "/";
                    res += std::to_string(p.second.expected);
                }
                res += " "; res += p.first;
            }
        return res;
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
