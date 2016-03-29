#pragma once

#include "sync.hh"
#include "util.hh"

#include <queue>
#include <functional>
#include <thread>

namespace nix {

/* A simple thread pool that executes a queue of work items
   (lambdas). */
class ThreadPool
{
public:

    ThreadPool(size_t nrThreads = 0);

    // FIXME: use std::packaged_task?
    typedef std::function<void()> work_t;

    /* Enqueue a function to be executed by the thread pool. */
    void enqueue(const work_t & t);

    /* Execute work items until the queue is empty. Note that work
       items are allowed to add new items to the queue; this is
       handled correctly. Queue processing stops prematurely if any
       work item throws an exception. This exception is propagated to
       the calling thread. If multiple work items throw an exception
       concurrently, only one item is propagated; the others are
       printed on stderr and otherwise ignored. */
    void process();

private:

    size_t nrThreads;

    struct State
    {
        std::queue<work_t> left;
        size_t pending = 0;
        std::exception_ptr exception;
    };

    Sync<State> state;

    std::condition_variable wakeup;

};

}
