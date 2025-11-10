#pragma once
///@file

#include <cstdlib>
#include <mutex>
#include <shared_mutex>
#include <condition_variable>
#include <cassert>

#include "nix/util/error.hh"

namespace nix {

/**
 * This template class ensures synchronized access to a value of type
 * T. It is used as follows:
 *
 *   struct Data { int x; ... };
 *
 *   Sync<Data> data;
 *
 *   {
 *     auto data_(data.lock());
 *     data_->x = 123;
 *   }
 *
 * Here, "data" is automatically unlocked when "data_" goes out of
 * scope.
 */
template<class T, class M, class WL, class RL>
class SyncBase
{
private:
    M mutex;
    T data;

public:

    using element_type = T;

    SyncBase() {}

    SyncBase(const T & data)
        : data(data)
    {
    }

    SyncBase(T && data) noexcept
        : data(std::move(data))
    {
    }

    SyncBase(SyncBase && other) noexcept
        : data(std::move(*other.lock()))
    {
    }

    template<class L>
    class Lock
    {
    protected:
        SyncBase * s;
        L lk;
        friend SyncBase;

        Lock(SyncBase * s)
            : s(s)
            , lk(s->mutex)
        {
        }
    public:
        Lock(Lock && l)
            : s(l.s)
        {
            unreachable();
        }

        Lock(const Lock & l) = delete;

        ~Lock() {}

        void wait(std::condition_variable & cv)
        {
            assert(s);
            cv.wait(lk);
        }

        template<class Rep, class Period>
        std::cv_status wait_for(std::condition_variable & cv, const std::chrono::duration<Rep, Period> & duration)
        {
            assert(s);
            return cv.wait_for(lk, duration);
        }

        template<class Rep, class Period, class Predicate>
        bool wait_for(std::condition_variable & cv, const std::chrono::duration<Rep, Period> & duration, Predicate pred)
        {
            assert(s);
            return cv.wait_for(lk, duration, pred);
        }

        template<class Clock, class Duration>
        std::cv_status
        wait_until(std::condition_variable & cv, const std::chrono::time_point<Clock, Duration> & duration)
        {
            assert(s);
            return cv.wait_until(lk, duration);
        }
    };

    struct WriteLock : Lock<WL>
    {
        T * operator->()
        {
            return &WriteLock::s->data;
        }

        T & operator*()
        {
            return WriteLock::s->data;
        }
    };

    /**
     * Acquire write (exclusive) access to the inner value.
     */
    WriteLock lock()
    {
        return WriteLock(this);
    }

    struct ReadLock : Lock<RL>
    {
        const T * operator->()
        {
            return &ReadLock::s->data;
        }

        const T & operator*()
        {
            return ReadLock::s->data;
        }
    };

    /**
     * Acquire read access to the inner value. When using
     * `std::shared_mutex`, this will use a shared lock.
     */
    ReadLock readLock() const
    {
        return ReadLock(const_cast<SyncBase *>(this));
    }
};

template<class T>
using Sync = SyncBase<T, std::mutex, std::unique_lock<std::mutex>, std::unique_lock<std::mutex>>;

template<class T>
using SharedSync =
    SyncBase<T, std::shared_mutex, std::unique_lock<std::shared_mutex>, std::shared_lock<std::shared_mutex>>;

} // namespace nix
