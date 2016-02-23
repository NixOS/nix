#pragma once

#include <mutex>
#include <condition_variable>
#include <cassert>

namespace nix {

/* This template class ensures synchronized access to a value of type
   T. It is used as follows:

     struct Data { int x; ... };

     Sync<Data> data;

     {
       auto data_(data.lock());
       data_->x = 123;
     }

   Here, "data" is automatically unlocked when "data_" goes out of
   scope.
*/

template<class T>
class Sync
{
private:
    std::mutex mutex;
    T data;

public:

    Sync() { }
    Sync(const T & data) : data(data) { }

    class Lock
    {
    private:
        Sync * s;
        friend Sync;
        Lock(Sync * s) : s(s) { s->mutex.lock(); }
    public:
        Lock(Lock && l) : s(l.s) { l.s = 0; }
        Lock(const Lock & l) = delete;
        ~Lock() { if (s) s->mutex.unlock(); }
        T * operator -> () { return &s->data; }
        T & operator * () { return s->data; }

        /* FIXME: performance impact of condition_variable_any? */
        void wait(std::condition_variable_any & cv)
        {
            assert(s);
            cv.wait(s->mutex);
        }

        template<class Rep, class Period, class Predicate>
        bool wait_for(std::condition_variable_any & cv,
            const std::chrono::duration<Rep, Period> & duration,
            Predicate pred)
        {
            assert(s);
            return cv.wait_for(s->mutex, duration, pred);
        }

        template<class Clock, class Duration>
        std::cv_status wait_until(std::condition_variable_any & cv,
            const std::chrono::time_point<Clock, Duration> & duration)
        {
            assert(s);
            return cv.wait_until(s->mutex, duration);
        }
    };

    Lock lock() { return Lock(this); }
};

}
