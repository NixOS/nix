#include <set>
#include <future>
#include "sync.hh"

using std::set;

namespace nix {

template<typename T>
using GetEdgesAsync = std::function<void(const T &, std::function<void(std::promise<set<T>> &)>)>;

template<typename T>
void computeClosure(
    const set<T> startElts,
    set<T> & res,
    GetEdgesAsync<T> getEdgesAsync
)
{
    struct State
    {
        size_t pending;
        set<T> & res;
        std::exception_ptr exc;
    };

    Sync<State> state_(State{0, res, 0});

    std::function<void(const T &)> enqueue;

    std::condition_variable done;

    enqueue = [&](const T & current) -> void {
        {
            auto state(state_.lock());
            if (state->exc) return;
            if (!state->res.insert(current).second) return;
            state->pending++;
        }

        getEdgesAsync(current, [&](std::promise<set<T>> & prom) {
            try {
                auto children = prom.get_future().get();
                for (auto & child : children)
                    enqueue(child);
                {
                    auto state(state_.lock());
                    assert(state->pending);
                    if (!--state->pending) done.notify_one();
                }
            } catch (...) {
                auto state(state_.lock());
                if (!state->exc) state->exc = std::current_exception();
                assert(state->pending);
                if (!--state->pending) done.notify_one();
            };
        });
    };

    for (auto & startElt : startElts)
        enqueue(startElt);

    {
        auto state(state_.lock());
        while (state->pending) state.wait(done);
        if (state->exc) std::rethrow_exception(state->exc);
    }
}

}
