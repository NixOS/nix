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

    debug("starting pool of %d threads", maxThreads - 1);
}

ThreadPool::~ThreadPool()
{
    shutdown();
}

void ThreadPool::shutdown()
{
    std::vector<std::thread> workers;
    {
        auto state(state_.lock());
        quit = true;
        std::swap(workers, state->workers);
    }

    if (workers.empty()) return;

    debug("reaping %d worker threads", workers.size());

    work.notify_all();

    for (auto & thr : workers)
        thr.join();
}

void ThreadPool::enqueue(const work_t & t)
{
    auto state(state_.lock());
    if (quit)
        throw ThreadPoolShutDown("cannot enqueue a work item while the thread pool is shutting down");
    state->pending.push(t);
    /* Note: process() also executes items, so count it as a worker. */
    if (state->pending.size() > state->workers.size() + 1 && state->workers.size() + 1 < maxThreads)
        state->workers.emplace_back(&ThreadPool::doWork, this, false);
    work.notify_one();
}

void ThreadPool::process()
{
    state_.lock()->draining = true;

    /* Do work until no more work is pending or active. */
    try {
        doWork(true);

        auto state(state_.lock());

        assert(quit);

        if (state->exception)
            std::rethrow_exception(state->exception);

    } catch (...) {
        /* In the exceptional case, some workers may still be
           active. They may be referencing the stack frame of the
           caller. So wait for them to finish. (~ThreadPool also does
           this, but it might be destroyed after objects referenced by
           the work item lambdas.) */
        shutdown();
        throw;
    }
}

void ThreadPool::doWork(bool mainThread)
{
    if (!mainThread)
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

            /* Wait until a work item is available or we're asked to
               quit. */
            while (true) {
                if (quit) return;

                if (!state->pending.empty()) break;

                /* If there are no active or pending items, and the
                   main thread is running process(), then no new items
                   can be added. So exit. */
                if (!state->active && state->draining) {
                    quit = true;
                    work.notify_all();
                    return;
                }

                state.wait(work);
            }

            w = std::move(state->pending.front());
            state->pending.pop();
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


