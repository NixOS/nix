#pragma once

namespace nix {

/**
 * A helper for `std::unique_ptr` that ensures that C APIs that require manual memory management get properly freed. The
 * template parameter `del` is a function that takes a pointer and frees it.
 */
template<auto del>
struct Deleter
{
    template<typename T>
    void operator()(T * p) const
    {
        del(p);
    };
};

} // namespace nix