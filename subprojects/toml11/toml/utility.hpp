//     Copyright Toru Niina 2017.
// Distributed under the MIT License.
#ifndef TOML11_UTILITY_HPP
#define TOML11_UTILITY_HPP
#include <memory>
#include <sstream>
#include <utility>

#include "traits.hpp"
#include "version.hpp"

#if TOML11_CPLUSPLUS_STANDARD_VERSION >= 201402L
#  define TOML11_MARK_AS_DEPRECATED(msg) [[deprecated(msg)]]
#elif defined(__GNUC__)
#  define TOML11_MARK_AS_DEPRECATED(msg) __attribute__((deprecated(msg)))
#elif defined(_MSC_VER)
#  define TOML11_MARK_AS_DEPRECATED(msg) __declspec(deprecated(msg))
#else
#  define TOML11_MARK_AS_DEPRECATED
#endif

namespace toml
{

#if TOML11_CPLUSPLUS_STANDARD_VERSION >= 201402L

using std::make_unique;

#else

template<typename T, typename ... Ts>
inline std::unique_ptr<T> make_unique(Ts&& ... args)
{
    return std::unique_ptr<T>(new T(std::forward<Ts>(args)...));
}

#endif // TOML11_CPLUSPLUS_STANDARD_VERSION >= 2014

namespace detail
{
template<typename Container>
void try_reserve_impl(Container& container, std::size_t N, std::true_type)
{
    container.reserve(N);
    return;
}
template<typename Container>
void try_reserve_impl(Container&, std::size_t, std::false_type) noexcept
{
    return;
}
} // detail

template<typename Container>
void try_reserve(Container& container, std::size_t N)
{
    if(N <= container.size()) {return;}
    detail::try_reserve_impl(container, N, detail::has_reserve_method<Container>{});
    return;
}

namespace detail
{
inline std::string concat_to_string_impl(std::ostringstream& oss)
{
    return oss.str();
}
template<typename T, typename ... Ts>
std::string concat_to_string_impl(std::ostringstream& oss, T&& head, Ts&& ... tail)
{
    oss << std::forward<T>(head);
    return concat_to_string_impl(oss, std::forward<Ts>(tail) ... );
}
} // detail

template<typename ... Ts>
std::string concat_to_string(Ts&& ... args)
{
    std::ostringstream oss;
    oss << std::boolalpha << std::fixed;
    return detail::concat_to_string_impl(oss, std::forward<Ts>(args) ...);
}

template<typename T>
T from_string(const std::string& str, T opt)
{
    T v(opt);
    std::istringstream iss(str);
    iss >> v;
    return v;
}

namespace detail
{
#if TOML11_CPLUSPLUS_STANDARD_VERSION >= 201402L
template<typename T>
decltype(auto) last_one(T&& tail) noexcept
{
    return std::forward<T>(tail);
}

template<typename T, typename ... Ts>
decltype(auto) last_one(T&& /*head*/, Ts&& ... tail) noexcept
{
    return last_one(std::forward<Ts>(tail)...);
}
#else // C++11
// The following code
// ```cpp
//  1 | template<typename T, typename ... Ts>
//  2 | auto last_one(T&& /*head*/, Ts&& ... tail)
//  3 |  -> decltype(last_one(std::forward<Ts>(tail)...))
//  4 | {
//  5 |     return last_one(std::forward<Ts>(tail)...);
//  6 | }
// ```
// does not work because the function `last_one(...)` is not yet defined at
// line #3, so `decltype()` cannot deduce the type returned from `last_one`.
// So we need to determine return type in a different way, like a meta func.

template<typename T, typename ... Ts>
struct last_one_in_pack
{
    using type = typename last_one_in_pack<Ts...>::type;
};
template<typename T>
struct last_one_in_pack<T>
{
    using type = T;
};
template<typename ... Ts>
using last_one_in_pack_t = typename last_one_in_pack<Ts...>::type;

template<typename T>
T&& last_one(T&& tail) noexcept
{
    return std::forward<T>(tail);
}
template<typename T, typename ... Ts>
enable_if_t<(sizeof...(Ts) > 0), last_one_in_pack_t<Ts&& ...>>
last_one(T&& /*head*/, Ts&& ... tail)
{
    return last_one(std::forward<Ts>(tail)...);
}

#endif
} // detail

}// toml
#endif // TOML11_UTILITY
