//     Copyright Toru Niina 2017.
// Distributed under the MIT License.
#ifndef TOML11_TRAITS_HPP
#define TOML11_TRAITS_HPP

#include "from.hpp"
#include "into.hpp"
#include "version.hpp"

#include <chrono>
#include <forward_list>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>

#if TOML11_CPLUSPLUS_STANDARD_VERSION >= 201703L
#if __has_include(<string_view>)
#include <string_view>
#endif // has_include(<string_view>)
#endif // cplusplus   >= C++17

namespace toml
{
template<typename C, template<typename ...> class T, template<typename ...> class A>
class basic_value;

namespace detail
{
// ---------------------------------------------------------------------------
// check whether type T is a kind of container/map class

struct has_iterator_impl
{
    template<typename T> static std::true_type  check(typename T::iterator*);
    template<typename T> static std::false_type check(...);
};
struct has_value_type_impl
{
    template<typename T> static std::true_type  check(typename T::value_type*);
    template<typename T> static std::false_type check(...);
};
struct has_key_type_impl
{
    template<typename T> static std::true_type  check(typename T::key_type*);
    template<typename T> static std::false_type check(...);
};
struct has_mapped_type_impl
{
    template<typename T> static std::true_type  check(typename T::mapped_type*);
    template<typename T> static std::false_type check(...);
};
struct has_reserve_method_impl
{
    template<typename T> static std::false_type check(...);
    template<typename T> static std::true_type  check(
        decltype(std::declval<T>().reserve(std::declval<std::size_t>()))*);
};
struct has_push_back_method_impl
{
    template<typename T> static std::false_type check(...);
    template<typename T> static std::true_type  check(
        decltype(std::declval<T>().push_back(std::declval<typename T::value_type>()))*);
};
struct is_comparable_impl
{
    template<typename T> static std::false_type check(...);
    template<typename T> static std::true_type  check(
        decltype(std::declval<T>() < std::declval<T>())*);
};

struct has_from_toml_method_impl
{
    template<typename T, typename C,
             template<typename ...> class Tb, template<typename ...> class A>
    static std::true_type  check(
        decltype(std::declval<T>().from_toml(
                std::declval<::toml::basic_value<C, Tb, A>>()))*);

    template<typename T, typename C,
             template<typename ...> class Tb, template<typename ...> class A>
    static std::false_type check(...);
};
struct has_into_toml_method_impl
{
    template<typename T>
    static std::true_type  check(decltype(std::declval<T>().into_toml())*);
    template<typename T>
    static std::false_type check(...);
};

struct has_specialized_from_impl
{
    template<typename T>
    static std::false_type check(...);
    template<typename T, std::size_t S = sizeof(::toml::from<T>)>
    static std::true_type check(::toml::from<T>*);
};
struct has_specialized_into_impl
{
    template<typename T>
    static std::false_type check(...);
    template<typename T, std::size_t S = sizeof(::toml::into<T>)>
    static std::true_type check(::toml::from<T>*);
};


/// Intel C++ compiler can not use decltype in parent class declaration, here
/// is a hack to work around it. https://stackoverflow.com/a/23953090/4692076
#ifdef __INTEL_COMPILER
#define decltype(...) std::enable_if<true, decltype(__VA_ARGS__)>::type
#endif

template<typename T>
struct has_iterator    : decltype(has_iterator_impl::check<T>(nullptr)){};
template<typename T>
struct has_value_type  : decltype(has_value_type_impl::check<T>(nullptr)){};
template<typename T>
struct has_key_type    : decltype(has_key_type_impl::check<T>(nullptr)){};
template<typename T>
struct has_mapped_type : decltype(has_mapped_type_impl::check<T>(nullptr)){};
template<typename T>
struct has_reserve_method : decltype(has_reserve_method_impl::check<T>(nullptr)){};
template<typename T>
struct has_push_back_method : decltype(has_push_back_method_impl::check<T>(nullptr)){};
template<typename T>
struct is_comparable : decltype(is_comparable_impl::check<T>(nullptr)){};

template<typename T, typename C,
         template<typename ...> class Tb, template<typename ...> class A>
struct has_from_toml_method
: decltype(has_from_toml_method_impl::check<T, C, Tb, A>(nullptr)){};

template<typename T>
struct has_into_toml_method
: decltype(has_into_toml_method_impl::check<T>(nullptr)){};

template<typename T>
struct has_specialized_from : decltype(has_specialized_from_impl::check<T>(nullptr)){};
template<typename T>
struct has_specialized_into : decltype(has_specialized_into_impl::check<T>(nullptr)){};

#ifdef __INTEL_COMPILER
#undef decltype
#endif

// ---------------------------------------------------------------------------
// C++17 and/or/not

#if TOML11_CPLUSPLUS_STANDARD_VERSION >= 201703L

using std::conjunction;
using std::disjunction;
using std::negation;

#else

template<typename ...> struct conjunction : std::true_type{};
template<typename T>   struct conjunction<T> : T{};
template<typename T, typename ... Ts>
struct conjunction<T, Ts...> :
    std::conditional<static_cast<bool>(T::value), conjunction<Ts...>, T>::type
{};

template<typename ...> struct disjunction : std::false_type{};
template<typename T>   struct disjunction<T> : T {};
template<typename T, typename ... Ts>
struct disjunction<T, Ts...> :
    std::conditional<static_cast<bool>(T::value), T, disjunction<Ts...>>::type
{};

template<typename T>
struct negation : std::integral_constant<bool, !static_cast<bool>(T::value)>{};

#endif

// ---------------------------------------------------------------------------
// type checkers

template<typename T> struct is_std_pair : std::false_type{};
template<typename T1, typename T2>
struct is_std_pair<std::pair<T1, T2>> : std::true_type{};

template<typename T> struct is_std_tuple : std::false_type{};
template<typename ... Ts>
struct is_std_tuple<std::tuple<Ts...>> : std::true_type{};

template<typename T> struct is_std_forward_list : std::false_type{};
template<typename T>
struct is_std_forward_list<std::forward_list<T>> : std::true_type{};

template<typename T> struct is_chrono_duration: std::false_type{};
template<typename Rep, typename Period>
struct is_chrono_duration<std::chrono::duration<Rep, Period>>: std::true_type{};

template<typename T>
struct is_map : conjunction< // map satisfies all the following conditions
    has_iterator<T>,         // has T::iterator
    has_value_type<T>,       // has T::value_type
    has_key_type<T>,         // has T::key_type
    has_mapped_type<T>       // has T::mapped_type
    >{};
template<typename T> struct is_map<T&>                : is_map<T>{};
template<typename T> struct is_map<T const&>          : is_map<T>{};
template<typename T> struct is_map<T volatile&>       : is_map<T>{};
template<typename T> struct is_map<T const volatile&> : is_map<T>{};

template<typename T>
struct is_container : conjunction<
    negation<is_map<T>>,                         // not a map
    negation<std::is_same<T, std::string>>,      // not a std::string
#if TOML11_CPLUSPLUS_STANDARD_VERSION >= 201703L
#if __has_include(<string_view>)
    negation<std::is_same<T, std::string_view>>, // not a std::string_view
#endif // has_include(<string_view>)
#endif
    has_iterator<T>,                             // has T::iterator
    has_value_type<T>                            // has T::value_type
    >{};
template<typename T> struct is_container<T&>                : is_container<T>{};
template<typename T> struct is_container<T const&>          : is_container<T>{};
template<typename T> struct is_container<T volatile&>       : is_container<T>{};
template<typename T> struct is_container<T const volatile&> : is_container<T>{};

template<typename T>
struct is_basic_value: std::false_type{};
template<typename T> struct is_basic_value<T&>                : is_basic_value<T>{};
template<typename T> struct is_basic_value<T const&>          : is_basic_value<T>{};
template<typename T> struct is_basic_value<T volatile&>       : is_basic_value<T>{};
template<typename T> struct is_basic_value<T const volatile&> : is_basic_value<T>{};
template<typename C, template<typename ...> class M, template<typename ...> class V>
struct is_basic_value<::toml::basic_value<C, M, V>>: std::true_type{};

// ---------------------------------------------------------------------------
// C++14 index_sequence

#if TOML11_CPLUSPLUS_STANDARD_VERSION >= 201402L

using std::index_sequence;
using std::make_index_sequence;

#else

template<std::size_t ... Ns> struct index_sequence{};

template<typename IS, std::size_t N> struct push_back_index_sequence{};
template<std::size_t N, std::size_t ... Ns>
struct push_back_index_sequence<index_sequence<Ns...>, N>
{
    typedef index_sequence<Ns..., N> type;
};

template<std::size_t N>
struct index_sequence_maker
{
    typedef typename push_back_index_sequence<
        typename index_sequence_maker<N-1>::type, N>::type type;
};
template<>
struct index_sequence_maker<0>
{
    typedef index_sequence<0> type;
};
template<std::size_t N>
using make_index_sequence = typename index_sequence_maker<N-1>::type;

#endif // cplusplus >= 2014

// ---------------------------------------------------------------------------
// C++14 enable_if_t

#if TOML11_CPLUSPLUS_STANDARD_VERSION >= 201402L

using std::enable_if_t;

#else

template<bool B, typename T>
using enable_if_t = typename std::enable_if<B, T>::type;

#endif // cplusplus >= 2014

// ---------------------------------------------------------------------------
// return_type_of_t

#if TOML11_CPLUSPLUS_STANDARD_VERSION >= 201703L && defined(__cpp_lib_is_invocable) && __cpp_lib_is_invocable>=201703

template<typename F, typename ... Args>
using return_type_of_t = std::invoke_result_t<F, Args...>;

#else
// result_of is deprecated after C++17
template<typename F, typename ... Args>
using return_type_of_t = typename std::result_of<F(Args...)>::type;

#endif

// ---------------------------------------------------------------------------
// is_string_literal
//
// to use this, pass `typename remove_reference<T>::type` to T.

template<typename T>
struct is_string_literal:
disjunction<
    std::is_same<const char*, T>,
    conjunction<
        std::is_array<T>,
        std::is_same<const char, typename std::remove_extent<T>::type>
        >
    >{};

// ---------------------------------------------------------------------------
// C++20 remove_cvref_t

template<typename T>
struct remove_cvref
{
    using type = typename std::remove_cv<
        typename std::remove_reference<T>::type>::type;
};

template<typename T>
using remove_cvref_t = typename remove_cvref<T>::type;

}// detail
}//toml
#endif // TOML_TRAITS
