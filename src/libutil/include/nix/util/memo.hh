#pragma once
///@file

#include "nix/util/fun.hh"

#include <memory>
#include <mutex>
#include <optional>

namespace nix {

/**
 * Memoize a `fun<T()>`.
 *
 * Copies of the returned `fun` share the same cache.
 * Thread-safe.
 */
template<typename T>
fun<T()> memo(fun<T()> f)
{
    struct State
    {
        fun<T()> compute;
        std::once_flag flag;
        std::optional<T> cached;

        explicit State(fun<T()> compute)
            : compute(std::move(compute))
        {
        }
    };

    auto state = std::shared_ptr<State>(new State(std::move(f)));
    return [state]() -> T {
        std::call_once(state->flag, [&]() { state->cached.emplace(state->compute()); });
        return *state->cached;
    };
}

} // namespace nix
