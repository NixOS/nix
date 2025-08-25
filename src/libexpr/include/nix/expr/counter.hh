#pragma once

namespace nix {

struct Counter
{
    using value_type = uint64_t;

    std::atomic<value_type> inner{0};

    static bool enabled;

    Counter() {}

    operator value_type() const noexcept
    {
        return inner;
    }

    void operator=(value_type n) noexcept
    {
        inner = n;
    }

    value_type load() const noexcept
    {
        return inner;
    }

    value_type operator++() noexcept
    {
        return enabled ? ++inner : 0;
    }

    value_type operator++(int) noexcept
    {
        return enabled ? inner++ : 0;
    }

    value_type operator--() noexcept
    {
        return enabled ? --inner : 0;
    }

    value_type operator--(int) noexcept
    {
        return enabled ? inner-- : 0;
    }

    value_type operator+=(value_type n) noexcept
    {
        return enabled ? inner += n : 0;
    }

    value_type operator-=(value_type n) noexcept
    {
        return enabled ? inner -= n : 0;
    }
};

} // namespace nix
