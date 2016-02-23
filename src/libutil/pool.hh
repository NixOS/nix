#pragma once

#include <memory>
#include <list>
#include <functional>

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

    typedef std::function<ref<R>()> Factory;

private:

    Factory factory;

    struct State
    {
        unsigned int count = 0;
        std::list<ref<R>> idle;
    };

    Sync<State> state;

public:

    Pool(const Factory & factory = []() { return make_ref<R>(); })
        : factory(factory)
    { }

    class Handle
    {
    private:
        Pool & pool;
        ref<R> r;

        friend Pool;

        Handle(Pool & pool, std::shared_ptr<R> r) : pool(pool), r(r) { }

    public:
        Handle(Handle && h) : pool(h.pool), r(h.r) { abort(); }

        Handle(const Handle & l) = delete;

        ~Handle()
        {
            auto state_(pool.state.lock());
            state_->idle.push_back(r);
        }

        R * operator -> () { return &*r; }
        R & operator * () { return *r; }
    };

    Handle get()
    {
        {
            auto state_(state.lock());
            if (!state_->idle.empty()) {
                auto p = state_->idle.back();
                state_->idle.pop_back();
                return Handle(*this, p);
            }
            state_->count++;
        }
        /* Note: we don't hold the lock while creating a new instance,
           because creation might take a long time. */
        return Handle(*this, factory());
    }

    unsigned int count()
    {
        auto state_(state.lock());
        return state_->count;
    }
};

}
