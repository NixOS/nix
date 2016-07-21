#pragma once

#include "sync.hh"
#include "util.hh"

#include <queue>
#include <functional>
#include <thread>
#include <map>

namespace nix {

MakeError(ThreadPoolShutDown, Error)

/* A simple thread pool that executes a queue of work items
   (lambdas). */
class ThreadPool
{
public:

    ThreadPool(size_t maxThreads = 0);

    ~ThreadPool();

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

    size_t maxThreads;

    struct State
    {
        std::queue<work_t> left;
        size_t pending = 0;
        std::exception_ptr exception;
        std::vector<std::thread> workers;
        bool quit = false;
    };

    Sync<State> state_;

    std::condition_variable work, done;

    void workerEntry();
};

/* Process in parallel a set of items of type T that have a partial
   ordering between them. Thus, any item is only processed after all
   its dependencies have been processed. */
template<typename T>
void processGraph(
    ThreadPool & pool,
    const std::set<T> & nodes,
    std::function<std::set<T>(const T &)> getEdges,
    std::function<void(const T &)> processNode)
{
    struct Graph {
        std::set<T> left;
        std::map<T, std::set<T>> refs, rrefs;
        std::function<void(T)> wrap;
    };

    ref<Sync<Graph>> graph_ = make_ref<Sync<Graph>>();

    auto wrapWork = [&pool, graph_, processNode](const T & node) {
        processNode(node);

        /* Enqueue work for all nodes that were waiting on this one. */
        {
            auto graph(graph_->lock());
            graph->left.erase(node);
            for (auto & rref : graph->rrefs[node]) {
                auto & refs(graph->refs[rref]);
                auto i = refs.find(node);
                assert(i != refs.end());
                refs.erase(i);
                if (refs.empty())
                    pool.enqueue(std::bind(graph->wrap, rref));
            }
        }
    };

    {
        auto graph(graph_->lock());
        graph->left = nodes;
        graph->wrap = wrapWork;
    }

    /* Build the dependency graph; enqueue all nodes with no
       dependencies. */
    for (auto & node : nodes) {
        auto refs = getEdges(node);
        {
            auto graph(graph_->lock());
            for (auto & ref : refs)
                if (ref != node && graph->left.count(ref)) {
                    graph->refs[node].insert(ref);
                    graph->rrefs[ref].insert(node);
                }
            if (graph->refs[node].empty())
                pool.enqueue(std::bind(graph->wrap, node));
        }
    }
}

}
