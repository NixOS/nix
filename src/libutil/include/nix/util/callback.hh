#pragma once
///@file

#include <cassert>
#include <future>
#include <functional>

#include "nix/util/fun.hh"

namespace nix {

/**
 * A callback is a wrapper around a lambda that accepts a valid of
 * type T or an exception. (We abuse std::future<T> to pass the value or
 * exception.)
 */
template<typename T>
class Callback
{
    nix::fun<void(std::future<T>)> fun;
    std::atomic_flag done = ATOMIC_FLAG_INIT;

public:

    Callback(nix::fun<void(std::future<T>)> fun)
        : fun(fun)
    {
    }

    // NOTE: std::function is noexcept move-constructible since C++20.
    Callback(Callback && callback) noexcept(std::is_nothrow_move_constructible_v<decltype(fun)>)
        : fun(std::move(callback.fun))
    {
        auto prev = callback.done.test_and_set();
        if (prev)
            done.test_and_set();
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

} // namespace nix
