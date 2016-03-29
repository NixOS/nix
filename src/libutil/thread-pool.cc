#include "thread-pool.hh"

namespace nix {

ThreadPool::ThreadPool(size_t _nrThreads)
    : nrThreads(_nrThreads)
{
    if (!nrThreads) {
        nrThreads = std::thread::hardware_concurrency();
        if (!nrThreads) nrThreads = 1;
    }
}

void ThreadPool::enqueue(const work_t & t)
{
    auto state_(state.lock());
    state_->left.push(t);
    wakeup.notify_one();
}

void ThreadPool::process()
{
    printMsg(lvlDebug, format("starting pool of %d threads") % nrThreads);

    std::vector<std::thread> workers;

    for (size_t n = 0; n < nrThreads; n++)
        workers.push_back(std::thread([&]() {
            bool first = true;

            while (true) {
                work_t work;
                {
                    auto state_(state.lock());
                    if (state_->exception) return;
                    if (!first) {
                        assert(state_->pending);
                        state_->pending--;
                    }
                    first = false;
                    while (state_->left.empty()) {
                        if (!state_->pending) {
                            wakeup.notify_all();
                            return;
                        }
                        if (state_->exception) return;
                        state_.wait(wakeup);
                    }
                    work = state_->left.front();
                    state_->left.pop();
                    state_->pending++;
                }

                try {
                    work();
                } catch (std::exception & e) {
                    auto state_(state.lock());
                    if (state_->exception)
                        printMsg(lvlError, format("error: %s") % e.what());
                    else {
                        state_->exception = std::current_exception();
                        wakeup.notify_all();
                    }
                }
            }

        }));

    for (auto & thr : workers)
        thr.join();

    {
        auto state_(state.lock());
        if (state_->exception)
            std::rethrow_exception(state_->exception);
    }
}

}


