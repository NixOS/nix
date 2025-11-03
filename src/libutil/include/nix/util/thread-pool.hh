#pragma once
///@file

#include "nix/util/error.hh"
#include "nix/util/sync.hh"

#include <queue>
#include <functional>
#include <thread>
#include <map>
#include <atomic>

namespace nix {

MakeError(ThreadPoolShutDown, Error);

/**
 * A simple thread pool that executes a queue of work items
 * (lambdas).
 */
class ThreadPool
{
public:

    ThreadPool(size_t maxThreads = 0);

    ~ThreadPool();

    /**
     * An individual work item.
     *
     * \todo use std::packaged_task?
     */
    typedef std::function<void()> work_t;

    /**
     * Enqueue a function to be executed by the thread pool.
     */
    void enqueue(work_t t);

    /**
     * Execute work items until the queue is empty.
     *
     * \note Note that work items are allowed to add new items to the
     * queue; this is handled correctly.
     *
     * Queue processing stops prematurely if any work item throws an
     * exception. This exception is propagated to the calling thread. If
     * multiple work items throw an exception concurrently, only one
     * item is propagated; the others are printed on stderr and
     * otherwise ignored.
     */
    void process();

private:

    size_t maxThreads;

    struct State
    {
        std::queue<work_t> pending;
        size_t active = 0;
        std::exception_ptr exception;
        std::vector<std::thread> workers;
        bool draining = false;
    };

    std::atomic_bool quit{false};

    Sync<State> state_;

    std::condition_variable work;

    void doWork(bool mainThread);

    void shutdown();
};

/**
 * Process in parallel a set of items of type T that have a partial
 * ordering between them. Thus, any item is only processed after all
 * its dependencies have been processed.
 */
template<typename T>
void processGraph(
    const std::set<T> & nodes,
    std::function<std::set<T>(const T &)> getEdges,
    std::function<void(const T &)> processNode)
{
    struct Graph
    {
        std::set<T> left;
        std::map<T, std::set<T>> refs, rrefs;
    };

    Sync<Graph> graph_(Graph{nodes, {}, {}});

    std::function<void(const T &)> worker;

    /* Create pool last to ensure threads are stopped before other destructors
     * run */
    ThreadPool pool;

    worker = [&](const T & node) {
        {
            auto graph(graph_.lock());
            auto i = graph->refs.find(node);
            if (i == graph->refs.end())
                goto getRefs;
            goto doWork;
        }

    getRefs: {
        auto refs = getEdges(node);
        refs.erase(node);

        {
            auto graph(graph_.lock());
            for (auto & ref : refs)
                if (graph->left.count(ref)) {
                    graph->refs[node].insert(ref);
                    graph->rrefs[ref].insert(node);
                }
            if (graph->refs[node].empty())
                goto doWork;
        }
    }

        return;

    doWork:
        processNode(node);

        /* Enqueue work for all nodes that were waiting on this one
           and have no unprocessed dependencies. */
        {
            auto graph(graph_.lock());
            for (auto & rref : graph->rrefs[node]) {
                auto & refs(graph->refs[rref]);
                auto i = refs.find(node);
                assert(i != refs.end());
                refs.erase(i);
                if (refs.empty())
                    pool.enqueue(std::bind(worker, rref));
            }
            graph->left.erase(node);
            graph->refs.erase(node);
            graph->rrefs.erase(node);
        }
    };

    for (auto & node : nodes) {
        try {
            pool.enqueue(std::bind(worker, std::ref(node)));
        } catch (ThreadPoolShutDown &) {
            /* Stop if the thread pool is shutting down. It means a
               previous work item threw an exception, so process()
               below will rethrow it. */
            break;
        }
    }

    pool.process();

    if (!graph_.lock()->left.empty())
        throw Error("graph processing incomplete (cyclic reference?)");
}

} // namespace nix
