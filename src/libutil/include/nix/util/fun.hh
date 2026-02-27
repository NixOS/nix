#pragma once
///@file
// Tests in: src/libutil-tests/fun.cc

#include <functional>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace nix {

/**
 * A non-nullable wrapper around `std::function`.
 *
 * Like `ref<T>` guarantees a non-null pointer, `fun<Sig>` guarantees
 * a non-null callable. Construction from an empty `std::function` or
 * `nullptr` is rejected.
 */
template<typename Sig>
class fun;

template<typename Ret, typename... Args>
class fun<Ret(Args...)>
{
private:

    std::function<Ret(Args...)> f;

    void assertCallable()
    {
        if (!f)
            throw std::invalid_argument("null callable cast to fun");
    }

public:

    using result_type = Ret;

    /**
     * Construct from any callable that `std::function` accepts.
     *
     * Excludes `fun` itself (handled by copy/move), `std::function`
     * (handled by explicit constructors below), and `nullptr`.
     */
    template<typename F>
        requires(
            !std::is_same_v<std::decay_t<F>, fun> && !std::is_same_v<std::decay_t<F>, std::function<Ret(Args...)>>
            && !std::is_same_v<std::decay_t<F>, std::nullptr_t>
            && std::is_constructible_v<std::function<Ret(Args...)>, F>)
    fun(F && callable)
        : f(std::forward<F>(callable))
    {
        assertCallable();
    }

    /**
     * Construct from an existing `std::function`.
     * Explicit because an empty `std::function` will throw.
     */
    explicit fun(const std::function<Ret(Args...)> & fn)
        : f(fn)
    {
        assertCallable();
    }

    explicit fun(std::function<Ret(Args...)> && fn)
        : f(std::move(fn))
    {
        assertCallable();
    }

    /** Prevent construction from nullptr at compile time. */
    fun(std::nullptr_t) = delete;

    template<typename... Ts>
    Ret operator()(Ts &&... args) const
    {
        return f(std::forward<Ts>(args)...);
    }

    /** Get the underlying `std::function`. */
    const std::function<Ret(Args...)> & get_fn() const &
    {
        return f;
    }

    std::function<Ret(Args...)> get_fn() &&
    {
        return std::move(f);
    }
};

} // namespace nix
