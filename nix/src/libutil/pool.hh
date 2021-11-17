#pragma once

#include <functional>
#include <limits>
#include <list>
#include <memory>
#include <cassert>

#include "sync.hh"
#include "ref.hh"

namespace nix {

/* This template class implements a simple pool manager of resources
   of some type R, such as database connections. It is used as
   follows:

     class Connection { ... };

     Pool<Connection> pool;

     {
       auto conn(pool.get());
       conn->exec("select ...");
     }

   Here, the Connection object referenced by ‘conn’ is automatically
   returned to the pool when ‘conn’ goes out of scope.
*/

template <class R>
class Pool
{
public:

    /* A function that produces new instances of R on demand. */
    typedef std::function<ref<R>()> Factory;

    /* A function that checks whether an instance of R is still
       usable. Unusable instances are removed from the pool. */
    typedef std::function<bool(const ref<R> &)> Validator;

private:

    Factory factory;
    Validator validator;

    struct State
    {
        size_t inUse = 0;
        size_t max;
        std::vector<ref<R>> idle;
    };

    Sync<State> state;

    std::condition_variable wakeup;

public:

    Pool(size_t max = std::numeric_limits<size_t>::max(),
        const Factory & factory = []() { return make_ref<R>(); },
        const Validator & validator = [](ref<R> r) { return true; })
        : factory(factory)
        , validator(validator)
    {
        auto state_(state.lock());
        state_->max = max;
    }

    void incCapacity()
    {
        auto state_(state.lock());
        state_->max++;
        /* we could wakeup here, but this is only used when we're
         * about to nest Pool usages, and we want to save the slot for
         * the nested use if we can
         */
    }

    void decCapacity()
    {
        auto state_(state.lock());
        state_->max--;
    }

    ~Pool()
    {
        auto state_(state.lock());
        assert(!state_->inUse);
        state_->max = 0;
        state_->idle.clear();
    }

    class Handle
    {
    private:
        Pool & pool;
        std::shared_ptr<R> r;
        bool bad = false;

        friend Pool;

        Handle(Pool & pool, std::shared_ptr<R> r) : pool(pool), r(r) { }

    public:
        Handle(Handle && h) : pool(h.pool), r(h.r) { h.r.reset(); }

        Handle(const Handle & l) = delete;

        ~Handle()
        {
            if (!r) return;
            {
                auto state_(pool.state.lock());
                if (!bad)
                    state_->idle.push_back(ref<R>(r));
                assert(state_->inUse);
                state_->inUse--;
            }
            pool.wakeup.notify_one();
        }

        R * operator -> () { return &*r; }
        R & operator * () { return *r; }

        void markBad() { bad = true; }
    };

    Handle get()
    {
        {
            auto state_(state.lock());

            /* If we're over the maximum number of instance, we need
               to wait until a slot becomes available. */
            while (state_->idle.empty() && state_->inUse >= state_->max)
                state_.wait(wakeup);

            while (!state_->idle.empty()) {
                auto p = state_->idle.back();
                state_->idle.pop_back();
                if (validator(p)) {
                    state_->inUse++;
                    return Handle(*this, p);
                }
            }

            state_->inUse++;
        }

        /* We need to create a new instance. Because that might take a
           while, we don't hold the lock in the meantime. */
        try {
            Handle h(*this, factory());
            return h;
        } catch (...) {
            auto state_(state.lock());
            state_->inUse--;
            wakeup.notify_one();
            throw;
        }
    }

    size_t count()
    {
        auto state_(state.lock());
        return state_->idle.size() + state_->inUse;
    }

    size_t capacity()
    {
        return state.lock()->max;
    }

    void flushBad()
    {
        auto state_(state.lock());
        std::vector<ref<R>> left;
        for (auto & p : state_->idle)
            if (validator(p))
                left.push_back(p);
        std::swap(state_->idle, left);
    }
};

}
