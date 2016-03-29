#include "progress-bar.hh"

#include <iostream>

namespace nix {

ProgressBar::ProgressBar()
{
    _writeToStderr = [&](const unsigned char * buf, size_t count) {
        auto state_(state.lock());
        assert(!state_->done);
        std::cerr << "\r\e[K" << std::string((const char *) buf, count);
        render(*state_);
    };
}

ProgressBar::~ProgressBar()
{
    done();
}

void ProgressBar::updateStatus(const std::string & s)
{
    auto state_(state.lock());
    assert(!state_->done);
    state_->status = s;
    render(*state_);
}

void ProgressBar::done()
{
    auto state_(state.lock());
    assert(state_->activities.empty());
    state_->done = true;
    std::cerr << "\r\e[K";
    std::cerr.flush();
    _writeToStderr = decltype(_writeToStderr)();
}

void ProgressBar::render(State & state_)
{
    std::cerr << '\r' << state_.status;
    if (!state_.activities.empty()) {
        if (!state_.status.empty()) std::cerr << ' ';
        std::cerr << *state_.activities.rbegin();
    }
    std::cerr << "\e[K";
    std::cerr.flush();
}


ProgressBar::Activity ProgressBar::startActivity(const FormatOrString & fs)
{
    return Activity(*this, fs);
}

ProgressBar::Activity::Activity(ProgressBar & pb, const FormatOrString & fs)
    : pb(pb)
{
    auto state_(pb.state.lock());
    state_->activities.push_back(fs.s);
    it = state_->activities.end(); --it;
    pb.render(*state_);
}

ProgressBar::Activity::~Activity()
{
    auto state_(pb.state.lock());
    state_->activities.erase(it);
}

}
