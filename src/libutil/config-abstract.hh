#pragma once
///@type

#include <optional>

namespace nix::config {

template<typename T>
struct JustValue
{
    T value;

    operator const T &() const
    {
        return value;
    }

    operator T &()
    {
        return value;
    }

    const T & get() const
    {
        return value;
    }

    bool operator==(auto && v2) const
    {
        return value == v2;
    }

    bool operator!=(auto && v2) const
    {
        return value != v2;
    }
};

template<typename T>
auto && operator<<(auto && str, const JustValue<T> & opt)
{
    return str << opt.get();
}

template<typename T>
struct OptValue
{
    std::optional<T> optValue;
};

}
