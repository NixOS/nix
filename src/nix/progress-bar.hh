#pragma once

#include "sync.hh"
#include "util.hh"

namespace nix {

class ProgressBar
{
private:
    struct State
    {
        std::string status;
        bool done = false;
        std::list<std::string> activities;
    };

    Sync<State> state;

public:

    ProgressBar();

    ~ProgressBar();

    void updateStatus(const std::string & s);

    void done();

    class Activity
    {
        friend class ProgressBar;
    private:
        ProgressBar & pb;
        std::list<std::string>::iterator it;
        Activity(ProgressBar & pb, const FormatOrString & fs);
    public:
        ~Activity();
    };

    Activity startActivity(const FormatOrString & fs);

private:

    void render(State & state_);

};

}
