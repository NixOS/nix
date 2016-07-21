#include "thread-pool.hh"
#include "affinity.hh"

namespace nix {

ThreadPool::ThreadPool(size_t _maxThreads)
    : maxThreads(_maxThreads)
{
    restoreAffinity(); // FIXME

    if (!maxThreads) {
        maxThreads = std::thread::hardware_concurrency();
        if (!maxThreads) maxThreads = 1;
    }

    debug(format("starting pool of %d threads") % maxThreads);
}

ThreadPool::~ThreadPool()
{
    std::vector<std::thread> workers;
    {
        auto state(state_.lock());
        state->quit = true;
        std::swap(workers, state->workers);
    }

    debug(format("reaping %d worker threads") % workers.size());

    work.notify_all();

    for (auto & thr : workers)
        thr.join();
}

void ThreadPool::enqueue(const work_t & t)
{
    auto state(state_.lock());
    if (state->quit)
        throw ThreadPoolShutDown("cannot enqueue a work item while the thread pool is shutting down");
    state->left.push(t);
    if (state->left.size() > state->workers.size() && state->workers.size() < maxThreads)
        state->workers.emplace_back(&ThreadPool::workerEntry, this);
    work.notify_one();
}

void ThreadPool::process()
{
    while (true) {
        auto state(state_.lock());
        if (state->exception)
            std::rethrow_exception(state->exception);
        if (state->left.empty() && !state->pending) break;
        state.wait(done);
    }
}

void ThreadPool::workerEntry()
{
    bool didWork = false;

    while (true) {
        work_t w;
        {
            auto state(state_.lock());
            while (true) {
                if (state->quit || state->exception) return;
                if (didWork) {
                    assert(state->pending);
                    state->pending--;
                    didWork = false;
                }
                if (!state->left.empty()) break;
                if (!state->pending)
                    done.notify_all();
                state.wait(work);
            }
            w = state->left.front();
            state->left.pop();
            state->pending++;
        }

        try {
            w();
        } catch (std::exception & e) {
            auto state(state_.lock());
            if (state->exception) {
                if (!dynamic_cast<Interrupted*>(&e) &&
                    !dynamic_cast<ThreadPoolShutDown*>(&e))
                    printMsg(lvlError, format("error: %s") % e.what());
            } else {
                state->exception = std::current_exception();
                work.notify_all();
                done.notify_all();
            }
        }

        didWork = true;
    }
}

}


