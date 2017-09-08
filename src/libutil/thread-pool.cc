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
        quit = true;
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
    if (quit)
        throw ThreadPoolShutDown("cannot enqueue a work item while the thread pool is shutting down");
    state->left.push(t);
    if (state->left.size() > state->workers.size() && state->workers.size() < maxThreads)
        state->workers.emplace_back(&ThreadPool::workerEntry, this);
    work.notify_one();
}

void ThreadPool::process()
{
    /* Loop until there are no active work items *and* there either
       are no queued items or there is an exception. The
       post-condition is that no new items will become active. */
    while (true) {
        auto state(state_.lock());
        if (!state->active) {
            if (state->exception)
                std::rethrow_exception(state->exception);
            if (state->left.empty())
                break;
        }
        state.wait(done);
    }
}

void ThreadPool::workerEntry()
{
    interruptCheck = [&]() { return (bool) quit; };

    bool didWork = false;
    std::exception_ptr exc;

    while (true) {
        work_t w;
        {
            auto state(state_.lock());

            if (didWork) {
                assert(state->active);
                state->active--;

                if (exc) {

                    if (!state->exception) {
                        state->exception = exc;
                        // Tell the other workers to quit.
                        quit = true;
                        work.notify_all();
                    } else {
                        /* Print the exception, since we can't
                           propagate it. */
                        try {
                            std::rethrow_exception(exc);
                        } catch (std::exception & e) {
                            if (!dynamic_cast<Interrupted*>(&e) &&
                                !dynamic_cast<ThreadPoolShutDown*>(&e))
                                ignoreException();
                        } catch (...) {
                        }
                    }
                }
            }

            /* Wait until a work item is available or another thread
               had an exception or we're asked to quit. */
            while (true) {
                if (quit) {
                    if (!state->active)
                        done.notify_one();
                    return;
                }
                if (!state->left.empty()) break;
                if (!state->active) {
                    done.notify_one();
                    return;
                }
                state.wait(work);
            }

            w = std::move(state->left.front());
            state->left.pop();
            state->active++;
        }

        try {
            w();
        } catch (...) {
            exc = std::current_exception();
        }

        didWork = true;
    }
}

}


