#pragma once
///@file

#include "nix/expr/value.hh"

namespace nix {

template<size_t N>
struct StringData::Static
{
    /**
     * @note Must be first to make layout compatible with StringData.
     */
    const size_t size = N - 1;
    char data[N];

    consteval Static(const char (&str)[N])
    {
        static_assert(N > 0);
        if (str[size] != '\0')
            throw;
        std::copy_n(str, N, data);
    }

    operator const StringData &() const &
    {
        static_assert(sizeof(decltype(*this)) >= sizeof(StringData));
        static_assert(alignof(decltype(*this)) == alignof(StringData));
        /* NOTE: This cast is somewhat on the fence of what's legal in C++.
           The question boils down to whether flexible array members are
           layout compatible with fixed-size arrays. This is a gray area, since
           FAMs are not standard anyway.
           */
        return *reinterpret_cast<const StringData *>(this);
    }
};

template<StringData::Static S>
const StringData & operator""_sds()
{
    return S;
}

} // namespace nix
