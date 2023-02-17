#pragma once

#include <future>
#include <functional>

namespace nix {

/* A callback is a wrapper around a lambda that accepts a valid of
   type T or an exception. (We abuse std::future<T> to pass the value or
   exception.) */
template<typename T>
class Callback
{
    std::function<void(std::future<T>)> fun;
    std::atomic_flag done = ATOMIC_FLAG_INIT;

public:

    Callback(std::function<void(std::future<T>)> fun) : fun(fun) { }

    Callback(Callback && callback) : fun(std::move(callback.fun))
    {
        auto prev = callback.done.test_and_set();
        if (prev) done.test_and_set();
    }

    void operator()(T && t) noexcept
    {
        auto prev = done.test_and_set();
        assert(!prev);
        std::promise<T> promise;
        promise.set_value(std::move(t));
        fun(promise.get_future());
    }

    void rethrow(const std::exception_ptr & exc = std::current_exception()) noexcept
    {
        auto prev = done.test_and_set();
        assert(!prev);
        std::promise<T> promise;
        promise.set_exception(exc);
        fun(promise.get_future());
    }
};

}
