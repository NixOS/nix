//     Copyright Toru Niina 2017.
// Distributed under the MIT License.
#ifndef TOML11_GET_HPP
#define TOML11_GET_HPP
#include <algorithm>

#include "from.hpp"
#include "result.hpp"
#include "value.hpp"

namespace toml
{

// ============================================================================
// exact toml::* type

template<typename T, typename C,
         template<typename ...> class M, template<typename ...> class V>
detail::enable_if_t<detail::is_exact_toml_type<T, basic_value<C, M, V>>::value, T> &
get(basic_value<C, M, V>& v)
{
    return v.template cast<detail::type_to_enum<T, basic_value<C, M, V>>::value>();
}

template<typename T, typename C,
         template<typename ...> class M, template<typename ...> class V>
detail::enable_if_t<detail::is_exact_toml_type<T, basic_value<C, M, V>>::value, T> const&
get(const basic_value<C, M, V>& v)
{
    return v.template cast<detail::type_to_enum<T, basic_value<C, M, V>>::value>();
}

template<typename T, typename C,
         template<typename ...> class M, template<typename ...> class V>
detail::enable_if_t<detail::is_exact_toml_type<T, basic_value<C, M, V>>::value, T>
get(basic_value<C, M, V>&& v)
{
    return T(std::move(v).template cast<detail::type_to_enum<T, basic_value<C, M, V>>::value>());
}

// ============================================================================
// T == toml::value; identity transformation.

template<typename T, typename C,
         template<typename ...> class M, template<typename ...> class V>
inline detail::enable_if_t<std::is_same<T, basic_value<C, M, V>>::value, T>&
get(basic_value<C, M, V>& v)
{
    return v;
}

template<typename T, typename C,
         template<typename ...> class M, template<typename ...> class V>
inline detail::enable_if_t<std::is_same<T, basic_value<C, M, V>>::value, T> const&
get(const basic_value<C, M, V>& v)
{
    return v;
}

template<typename T, typename C,
         template<typename ...> class M, template<typename ...> class V>
inline detail::enable_if_t<std::is_same<T, basic_value<C, M, V>>::value, T>
get(basic_value<C, M, V>&& v)
{
    return basic_value<C, M, V>(std::move(v));
}

// ============================================================================
// T == toml::basic_value<C2, M2, V2>; basic_value -> basic_value

template<typename T, typename C,
         template<typename ...> class M, template<typename ...> class V>
inline detail::enable_if_t<detail::conjunction<detail::is_basic_value<T>,
    detail::negation<std::is_same<T, basic_value<C, M, V>>>
    >::value, T>
get(const basic_value<C, M, V>& v)
{
    return T(v);
}

// ============================================================================
// integer convertible from toml::Integer

template<typename T, typename C,
         template<typename ...> class M, template<typename ...> class V>
inline detail::enable_if_t<detail::conjunction<
    std::is_integral<T>,                            // T is integral
    detail::negation<std::is_same<T, bool>>,        // but not bool
    detail::negation<                               // but not toml::integer
        detail::is_exact_toml_type<T, basic_value<C, M, V>>>
    >::value, T>
get(const basic_value<C, M, V>& v)
{
    return static_cast<T>(v.as_integer());
}

// ============================================================================
// floating point convertible from toml::Float

template<typename T, typename C,
         template<typename ...> class M, template<typename ...> class V>
inline detail::enable_if_t<detail::conjunction<
    std::is_floating_point<T>,                      // T is floating_point
    detail::negation<                               // but not toml::floating
        detail::is_exact_toml_type<T, basic_value<C, M, V>>>
    >::value, T>
get(const basic_value<C, M, V>& v)
{
    return static_cast<T>(v.as_floating());
}

// ============================================================================
// std::string; toml uses its own toml::string, but it should be convertible to
// std::string seamlessly

template<typename T, typename C,
         template<typename ...> class M, template<typename ...> class V>
inline detail::enable_if_t<std::is_same<T, std::string>::value, std::string>&
get(basic_value<C, M, V>& v)
{
    return v.as_string().str;
}

template<typename T, typename C,
         template<typename ...> class M, template<typename ...> class V>
inline detail::enable_if_t<std::is_same<T, std::string>::value, std::string> const&
get(const basic_value<C, M, V>& v)
{
    return v.as_string().str;
}

template<typename T, typename C,
         template<typename ...> class M, template<typename ...> class V>
inline detail::enable_if_t<std::is_same<T, std::string>::value, std::string>
get(basic_value<C, M, V>&& v)
{
    return std::string(std::move(v.as_string().str));
}

// ============================================================================
// std::string_view

#if defined(TOML11_USING_STRING_VIEW) && TOML11_USING_STRING_VIEW>0
template<typename T, typename C,
         template<typename ...> class M, template<typename ...> class V>
inline detail::enable_if_t<std::is_same<T, std::string_view>::value, std::string_view>
get(const basic_value<C, M, V>& v)
{
    return std::string_view(v.as_string().str);
}
#endif

// ============================================================================
// std::chrono::duration from toml::local_time.

template<typename T, typename C,
         template<typename ...> class M, template<typename ...> class V>
inline detail::enable_if_t<detail::is_chrono_duration<T>::value, T>
get(const basic_value<C, M, V>& v)
{
    return std::chrono::duration_cast<T>(
            std::chrono::nanoseconds(v.as_local_time()));
}

// ============================================================================
// std::chrono::system_clock::time_point from toml::datetime variants

template<typename T, typename C,
         template<typename ...> class M, template<typename ...> class V>
inline detail::enable_if_t<
    std::is_same<std::chrono::system_clock::time_point, T>::value, T>
get(const basic_value<C, M, V>& v)
{
    switch(v.type())
    {
        case value_t::local_date:
        {
            return std::chrono::system_clock::time_point(v.as_local_date());
        }
        case value_t::local_datetime:
        {
            return std::chrono::system_clock::time_point(v.as_local_datetime());
        }
        case value_t::offset_datetime:
        {
            return std::chrono::system_clock::time_point(v.as_offset_datetime());
        }
        default:
        {
            throw type_error(detail::format_underline("toml::value: "
                "bad_cast to std::chrono::system_clock::time_point", {
                    {v.location(), concat_to_string("the actual type is ", v.type())}
                }), v.location());
        }
    }
}

// ============================================================================
// forward declaration to use this recursively. ignore this and go ahead.

// array-like type with push_back(value) method
template<typename T, typename C,
         template<typename ...> class M, template<typename ...> class V>
detail::enable_if_t<detail::conjunction<
    detail::is_container<T>,         // T is a container
    detail::has_push_back_method<T>, // T::push_back(value) works
    detail::negation<                // but not toml::array
        detail::is_exact_toml_type<T, basic_value<C, M, V>>>
    >::value, T>
get(const basic_value<C, M, V>&);

// array-like type without push_back(value) method
template<typename T, typename C,
         template<typename ...> class M, template<typename ...> class V>
detail::enable_if_t<detail::conjunction<
    detail::is_container<T>,                           // T is a container
    detail::negation<detail::has_push_back_method<T>>, // w/o push_back(...)
    detail::negation<detail::has_specialized_from<T>>, // T does not have special conversion
    detail::negation<                                  // not toml::array
        detail::is_exact_toml_type<T, basic_value<C, M, V>>>
    >::value, T>
get(const basic_value<C, M, V>&);

// std::pair<T1, T2>
template<typename T, typename C,
         template<typename ...> class M, template<typename ...> class V>
detail::enable_if_t<detail::is_std_pair<T>::value, T>
get(const basic_value<C, M, V>&);

// std::tuple<T1, T2, ...>
template<typename T, typename C,
         template<typename ...> class M, template<typename ...> class V>
detail::enable_if_t<detail::is_std_tuple<T>::value, T>
get(const basic_value<C, M, V>&);

// map-like classes
template<typename T, typename C,
         template<typename ...> class M, template<typename ...> class V>
detail::enable_if_t<detail::conjunction<
    detail::is_map<T>, // T is map
    detail::negation<  // but not toml::table
        detail::is_exact_toml_type<T, basic_value<C, M, V>>>
    >::value, T>
get(const basic_value<C, M, V>&);

// T.from_toml(v)
template<typename T, typename C,
         template<typename ...> class M, template<typename ...> class V>
detail::enable_if_t<detail::conjunction<
    detail::negation<                         // not a toml::* type
        detail::is_exact_toml_type<T, basic_value<C, M, V>>>,
    detail::has_from_toml_method<T, C, M, V>, // but has from_toml(toml::value)
    std::is_default_constructible<T>          // and default constructible
    >::value, T>
get(const basic_value<C, M, V>&);

// toml::from<T>::from_toml(v)
template<typename T, typename C,
         template<typename ...> class M, template<typename ...> class V>
detail::enable_if_t<detail::has_specialized_from<T>::value, T>
get(const basic_value<C, M, V>&);

template<typename T, typename C,
         template<typename ...> class M, template<typename ...> class V>
detail::enable_if_t<detail::has_specialized_from<T>::value, T>
get(basic_value<C, M, V>&);

// T(const toml::value&) and T is not toml::basic_value,
// and it does not have `from<T>` nor `from_toml`.
template<typename T, typename C,
         template<typename ...> class M, template<typename ...> class V>
detail::enable_if_t<detail::conjunction<
    detail::negation<detail::is_basic_value<T>>,
    std::is_constructible<T, const basic_value<C, M, V>&>,
    detail::negation<detail::has_from_toml_method<T, C, M, V>>,
    detail::negation<detail::has_specialized_from<T>>
    >::value, T>
get(const basic_value<C, M, V>&);

template<typename T, typename C,
         template<typename ...> class M, template<typename ...> class V>
detail::enable_if_t<detail::conjunction<
    detail::negation<detail::is_basic_value<T>>,
    std::is_constructible<T, basic_value<C, M, V>&>,
    detail::negation<detail::has_from_toml_method<T, C, M, V>>,
    detail::negation<detail::has_specialized_from<T>>
    >::value, T>
get(basic_value<C, M, V>&);

// ============================================================================
// array-like types; most likely STL container, like std::vector, etc.

template<typename T, typename C,
         template<typename ...> class M, template<typename ...> class V>
detail::enable_if_t<detail::conjunction<
    detail::is_container<T>,         // T is a container
    detail::has_push_back_method<T>, // container.push_back(elem) works
    detail::negation<                // but not toml::array
        detail::is_exact_toml_type<T, basic_value<C, M, V>>>
    >::value, T>
get(const basic_value<C, M, V>& v)
{
    using value_type = typename T::value_type;
    const auto& ary = v.as_array();

    T container;
    try_reserve(container, ary.size());

    for(const auto& elem : ary)
    {
        container.push_back(get<value_type>(elem));
    }
    return container;
}

// ============================================================================
// std::forward_list does not have push_back, insert, or emplace.
// It has insert_after, emplace_after, push_front.

template<typename T, typename C,
         template<typename ...> class M, template<typename ...> class V>
detail::enable_if_t<detail::is_std_forward_list<T>::value, T>
get(const basic_value<C, M, V>& v)
{
    using value_type = typename T::value_type;
    T container;
    for(const auto& elem : v.as_array())
    {
        container.push_front(get<value_type>(elem));
    }
    container.reverse();
    return container;
}

// ============================================================================
// array-like types, without push_back(). most likely [std|boost]::array.

template<typename T, typename C,
         template<typename ...> class M, template<typename ...> class V>
detail::enable_if_t<detail::conjunction<
    detail::is_container<T>,                           // T is a container
    detail::negation<detail::has_push_back_method<T>>, // w/o push_back
    detail::negation<detail::has_specialized_from<T>>, // T does not have special conversion
    detail::negation<                                  // T is not toml::array
        detail::is_exact_toml_type<T, basic_value<C, M, V>>>
    >::value, T>
get(const basic_value<C, M, V>& v)
{
    using value_type = typename T::value_type;
    const auto& ar = v.as_array();

    T container;
    if(ar.size() != container.size())
    {
        throw std::out_of_range(detail::format_underline(concat_to_string(
            "toml::get: specified container size is ", container.size(),
            " but there are ", ar.size(), " elements in toml array."), {
                {v.location(), "here"}
            }));
    }
    for(std::size_t i=0; i<ar.size(); ++i)
    {
        container[i] = ::toml::get<value_type>(ar[i]);
    }
    return container;
}

// ============================================================================
// std::pair.

template<typename T, typename C,
         template<typename ...> class M, template<typename ...> class V>
detail::enable_if_t<detail::is_std_pair<T>::value, T>
get(const basic_value<C, M, V>& v)
{
    using first_type  = typename T::first_type;
    using second_type = typename T::second_type;

    const auto& ar = v.as_array();
    if(ar.size() != 2)
    {
        throw std::out_of_range(detail::format_underline(concat_to_string(
            "toml::get: specified std::pair but there are ", ar.size(),
            " elements in toml array."), {{v.location(), "here"}}));
    }
    return std::make_pair(::toml::get<first_type >(ar.at(0)),
                          ::toml::get<second_type>(ar.at(1)));
}

// ============================================================================
// std::tuple.

namespace detail
{
template<typename T, typename Array, std::size_t ... I>
T get_tuple_impl(const Array& a, index_sequence<I...>)
{
    return std::make_tuple(
        ::toml::get<typename std::tuple_element<I, T>::type>(a.at(I))...);
}
} // detail

template<typename T, typename C,
         template<typename ...> class M, template<typename ...> class V>
detail::enable_if_t<detail::is_std_tuple<T>::value, T>
get(const basic_value<C, M, V>& v)
{
    const auto& ar = v.as_array();
    if(ar.size() != std::tuple_size<T>::value)
    {
        throw std::out_of_range(detail::format_underline(concat_to_string(
            "toml::get: specified std::tuple with ",
            std::tuple_size<T>::value, " elements, but there are ", ar.size(),
            " elements in toml array."), {{v.location(), "here"}}));
    }
    return detail::get_tuple_impl<T>(ar,
            detail::make_index_sequence<std::tuple_size<T>::value>{});
}

// ============================================================================
// map-like types; most likely STL map, like std::map or std::unordered_map.

template<typename T, typename C,
         template<typename ...> class M, template<typename ...> class V>
detail::enable_if_t<detail::conjunction<
    detail::is_map<T>, // T is map
    detail::negation<  // but not toml::array
        detail::is_exact_toml_type<T, basic_value<C, M, V>>>
    >::value, T>
get(const basic_value<C, M, V>& v)
{
    using key_type    = typename T::key_type;
    using mapped_type = typename T::mapped_type;
    static_assert(std::is_convertible<std::string, key_type>::value,
                  "toml::get only supports map type of which key_type is "
                  "convertible from std::string.");
    T map;
    for(const auto& kv : v.as_table())
    {
        map.emplace(key_type(kv.first), get<mapped_type>(kv.second));
    }
    return map;
}

// ============================================================================
// user-defined, but compatible types.

template<typename T, typename C,
         template<typename ...> class M, template<typename ...> class V>
detail::enable_if_t<detail::conjunction<
    detail::negation<                         // not a toml::* type
        detail::is_exact_toml_type<T, basic_value<C, M, V>>>,
    detail::has_from_toml_method<T, C, M, V>, // but has from_toml(toml::value) memfn
    std::is_default_constructible<T>          // and default constructible
    >::value, T>
get(const basic_value<C, M, V>& v)
{
    T ud;
    ud.from_toml(v);
    return ud;
}
template<typename T, typename C,
         template<typename ...> class M, template<typename ...> class V>
detail::enable_if_t<detail::has_specialized_from<T>::value, T>
get(const basic_value<C, M, V>& v)
{
    return ::toml::from<T>::from_toml(v);
}
template<typename T, typename C,
         template<typename ...> class M, template<typename ...> class V>
detail::enable_if_t<detail::has_specialized_from<T>::value, T>
get(basic_value<C, M, V>& v)
{
    return ::toml::from<T>::from_toml(v);
}

template<typename T, typename C,
         template<typename ...> class M, template<typename ...> class V>
detail::enable_if_t<detail::conjunction<
    detail::negation<detail::is_basic_value<T>>,                // T is not a toml::value
    std::is_constructible<T, const basic_value<C, M, V>&>,      // T is constructible from toml::value
    detail::negation<detail::has_from_toml_method<T, C, M, V>>, // and T does not have T.from_toml(v);
    detail::negation<detail::has_specialized_from<T>>           // and T does not have toml::from<T>{};
    >::value, T>
get(const basic_value<C, M, V>& v)
{
    return T(v);
}

template<typename T, typename C,
         template<typename ...> class M, template<typename ...> class V>
detail::enable_if_t<detail::conjunction<
    detail::negation<detail::is_basic_value<T>>,                // T is not a toml::value
    std::is_constructible<T, basic_value<C, M, V>&>,      // T is constructible from toml::value
    detail::negation<detail::has_from_toml_method<T, C, M, V>>, // and T does not have T.from_toml(v);
    detail::negation<detail::has_specialized_from<T>>           // and T does not have toml::from<T>{};
    >::value, T>
get(basic_value<C, M, V>& v)
{
    return T(v);
}

// ============================================================================
// find

// ----------------------------------------------------------------------------
// these overloads do not require to set T. and returns value itself.
template<typename C,
         template<typename ...> class M, template<typename ...> class V>
basic_value<C, M, V> const& find(const basic_value<C, M, V>& v, const key& ky)
{
    const auto& tab = v.as_table();
    if(tab.count(ky) == 0)
    {
        detail::throw_key_not_found_error(v, ky);
    }
    return tab.at(ky);
}
template<typename C,
         template<typename ...> class M, template<typename ...> class V>
basic_value<C, M, V>& find(basic_value<C, M, V>& v, const key& ky)
{
    auto& tab = v.as_table();
    if(tab.count(ky) == 0)
    {
        detail::throw_key_not_found_error(v, ky);
    }
    return tab.at(ky);
}
template<typename C,
         template<typename ...> class M, template<typename ...> class V>
basic_value<C, M, V> find(basic_value<C, M, V>&& v, const key& ky)
{
    typename basic_value<C, M, V>::table_type tab = std::move(v).as_table();
    if(tab.count(ky) == 0)
    {
        detail::throw_key_not_found_error(v, ky);
    }
    return basic_value<C, M, V>(std::move(tab.at(ky)));
}

// ----------------------------------------------------------------------------
// find(value, idx)
template<typename C,
         template<typename ...> class M, template<typename ...> class V>
basic_value<C, M, V> const&
find(const basic_value<C, M, V>& v, const std::size_t idx)
{
    const auto& ary = v.as_array();
    if(ary.size() <= idx)
    {
        throw std::out_of_range(detail::format_underline(concat_to_string(
            "index ", idx, " is out of range"), {{v.location(), "in this array"}}));
    }
    return ary.at(idx);
}
template<typename C,
         template<typename ...> class M, template<typename ...> class V>
basic_value<C, M, V>& find(basic_value<C, M, V>& v, const std::size_t idx)
{
    auto& ary = v.as_array();
    if(ary.size() <= idx)
    {
        throw std::out_of_range(detail::format_underline(concat_to_string(
            "index ", idx, " is out of range"), {{v.location(), "in this array"}}));
    }
    return ary.at(idx);
}
template<typename C,
         template<typename ...> class M, template<typename ...> class V>
basic_value<C, M, V> find(basic_value<C, M, V>&& v, const std::size_t idx)
{
    auto& ary = v.as_array();
    if(ary.size() <= idx)
    {
        throw std::out_of_range(detail::format_underline(concat_to_string(
            "index ", idx, " is out of range"), {{v.location(), "in this array"}}));
    }
    return basic_value<C, M, V>(std::move(ary.at(idx)));
}

// ----------------------------------------------------------------------------
// find<T>(value, key);

template<typename T, typename C,
         template<typename ...> class M, template<typename ...> class V>
decltype(::toml::get<T>(std::declval<basic_value<C, M, V> const&>()))
find(const basic_value<C, M, V>& v, const key& ky)
{
    const auto& tab = v.as_table();
    if(tab.count(ky) == 0)
    {
        detail::throw_key_not_found_error(v, ky);
    }
    return ::toml::get<T>(tab.at(ky));
}

template<typename T, typename C,
         template<typename ...> class M, template<typename ...> class V>
decltype(::toml::get<T>(std::declval<basic_value<C, M, V>&>()))
find(basic_value<C, M, V>& v, const key& ky)
{
    auto& tab = v.as_table();
    if(tab.count(ky) == 0)
    {
        detail::throw_key_not_found_error(v, ky);
    }
    return ::toml::get<T>(tab.at(ky));
}

template<typename T, typename C,
         template<typename ...> class M, template<typename ...> class V>
decltype(::toml::get<T>(std::declval<basic_value<C, M, V>&&>()))
find(basic_value<C, M, V>&& v, const key& ky)
{
    typename basic_value<C, M, V>::table_type tab = std::move(v).as_table();
    if(tab.count(ky) == 0)
    {
        detail::throw_key_not_found_error(v, ky);
    }
    return ::toml::get<T>(std::move(tab.at(ky)));
}

// ----------------------------------------------------------------------------
// find<T>(value, idx)
template<typename T, typename C,
         template<typename ...> class M, template<typename ...> class V>
decltype(::toml::get<T>(std::declval<basic_value<C, M, V> const&>()))
find(const basic_value<C, M, V>& v, const std::size_t idx)
{
    const auto& ary = v.as_array();
    if(ary.size() <= idx)
    {
        throw std::out_of_range(detail::format_underline(concat_to_string(
            "index ", idx, " is out of range"), {{v.location(), "in this array"}}));
    }
    return ::toml::get<T>(ary.at(idx));
}
template<typename T, typename C,
         template<typename ...> class M, template<typename ...> class V>
decltype(::toml::get<T>(std::declval<basic_value<C, M, V>&>()))
find(basic_value<C, M, V>& v, const std::size_t idx)
{
    auto& ary = v.as_array();
    if(ary.size() <= idx)
    {
        throw std::out_of_range(detail::format_underline(concat_to_string(
            "index ", idx, " is out of range"), {{v.location(), "in this array"}}));
    }
    return ::toml::get<T>(ary.at(idx));
}
template<typename T, typename C,
         template<typename ...> class M, template<typename ...> class V>
decltype(::toml::get<T>(std::declval<basic_value<C, M, V>&&>()))
find(basic_value<C, M, V>&& v, const std::size_t idx)
{
    typename basic_value<C, M, V>::array_type ary = std::move(v).as_array();
    if(ary.size() <= idx)
    {
        throw std::out_of_range(detail::format_underline(concat_to_string(
            "index ", idx, " is out of range"), {{v.location(), "in this array"}}));
    }
    return ::toml::get<T>(std::move(ary.at(idx)));
}

// --------------------------------------------------------------------------
// toml::find(toml::value, toml::key, Ts&& ... keys)

namespace detail
{
// It suppresses warnings by -Wsign-conversion. Let's say we have the following
// code.
// ```cpp
// const auto x = toml::find<std::string>(data, "array", 0);
// ```
// Here, the type of literal number `0` is `int`. `int` is a signed integer.
// `toml::find` takes `std::size_t` as an index. So it causes implicit sign
// conversion and `-Wsign-conversion` warns about it. Using `0u` instead of `0`
// suppresses the warning, but it makes user code messy.
//     To suppress this warning, we need to be aware of type conversion caused
// by `toml::find(v, key1, key2, ... keys)`. But the thing is that the types of
// keys can be any combination of {string-like, size_t-like}. Of course we can't
// write down all the combinations. Thus we need to use some function that
// recognize the type of argument and cast it into `std::string` or
// `std::size_t` depending on the context.
//     `key_cast` does the job. It has 2 overloads. One is invoked when the
// argument type is an integer and cast the argument into `std::size_t`. The
// other is invoked when the argument type is not an integer, possibly one of
// std::string, const char[N] or const char*, and construct std::string from
// the argument.
//     `toml::find(v, k1, k2, ... ks)` uses `key_cast` before passing `ks` to
// `toml::find(v, k)` to suppress -Wsign-conversion.

template<typename T>
enable_if_t<conjunction<std::is_integral<remove_cvref_t<T>>,
            negation<std::is_same<remove_cvref_t<T>, bool>>>::value, std::size_t>
key_cast(T&& v) noexcept
{
    return std::size_t(v);
}
template<typename T>
enable_if_t<negation<conjunction<std::is_integral<remove_cvref_t<T>>,
            negation<std::is_same<remove_cvref_t<T>, bool>>>>::value, std::string>
key_cast(T&& v) noexcept
{
    return std::string(std::forward<T>(v));
}
} // detail

template<typename C,
         template<typename ...> class M, template<typename ...> class V,
         typename Key1, typename Key2, typename ... Keys>
const basic_value<C, M, V>&
find(const basic_value<C, M, V>& v, Key1&& k1, Key2&& k2, Keys&& ... keys)
{
    return ::toml::find(::toml::find(v, detail::key_cast(k1)),
            detail::key_cast(k2), std::forward<Keys>(keys)...);
}
template<typename C,
         template<typename ...> class M, template<typename ...> class V,
         typename Key1, typename Key2, typename ... Keys>
basic_value<C, M, V>&
find(basic_value<C, M, V>& v, Key1&& k1, Key2&& k2, Keys&& ... keys)
{
    return ::toml::find(::toml::find(v, detail::key_cast(k1)),
            detail::key_cast(k2), std::forward<Keys>(keys)...);
}
template<typename C,
         template<typename ...> class M, template<typename ...> class V,
         typename Key1, typename Key2, typename ... Keys>
basic_value<C, M, V>
find(basic_value<C, M, V>&& v, Key1&& k1, Key2&& k2, Keys&& ... keys)
{
    return ::toml::find(::toml::find(std::move(v), std::forward<Key1>(k1)),
            detail::key_cast(k2), std::forward<Keys>(keys)...);
}

template<typename T, typename C,
         template<typename ...> class M, template<typename ...> class V,
         typename Key1, typename Key2, typename ... Keys>
decltype(::toml::get<T>(std::declval<const basic_value<C, M, V>&>()))
find(const basic_value<C, M, V>& v, Key1&& k1, Key2&& k2, Keys&& ... keys)
{
    return ::toml::find<T>(::toml::find(v, detail::key_cast(k1)),
            detail::key_cast(k2), std::forward<Keys>(keys)...);
}
template<typename T, typename C,
         template<typename ...> class M, template<typename ...> class V,
         typename Key1, typename Key2, typename ... Keys>
decltype(::toml::get<T>(std::declval<basic_value<C, M, V>&>()))
find(basic_value<C, M, V>& v, Key1&& k1, Key2&& k2, Keys&& ... keys)
{
    return ::toml::find<T>(::toml::find(v, detail::key_cast(k1)),
            detail::key_cast(k2), std::forward<Keys>(keys)...);
}
template<typename T, typename C,
         template<typename ...> class M, template<typename ...> class V,
         typename Key1, typename Key2, typename ... Keys>
decltype(::toml::get<T>(std::declval<basic_value<C, M, V>&&>()))
find(basic_value<C, M, V>&& v, Key1&& k1, Key2&& k2, Keys&& ... keys)
{
    return ::toml::find<T>(::toml::find(std::move(v), detail::key_cast(k1)),
            detail::key_cast(k2), std::forward<Keys>(keys)...);
}

// ============================================================================
// get_or(value, fallback)

template<typename C,
         template<typename ...> class M, template<typename ...> class V>
basic_value<C, M, V> const&
get_or(const basic_value<C, M, V>& v, const basic_value<C, M, V>&)
{
    return v;
}
template<typename C,
         template<typename ...> class M, template<typename ...> class V>
basic_value<C, M, V>&
get_or(basic_value<C, M, V>& v, basic_value<C, M, V>&)
{
    return v;
}
template<typename C,
         template<typename ...> class M, template<typename ...> class V>
basic_value<C, M, V>
get_or(basic_value<C, M, V>&& v, basic_value<C, M, V>&&)
{
    return v;
}

// ----------------------------------------------------------------------------
// specialization for the exact toml types (return type becomes lvalue ref)

template<typename T, typename C,
         template<typename ...> class M, template<typename ...> class V>
detail::enable_if_t<
    detail::is_exact_toml_type<T, basic_value<C, M, V>>::value, T> const&
get_or(const basic_value<C, M, V>& v, const T& opt)
{
    try
    {
        return get<detail::remove_cvref_t<T>>(v);
    }
    catch(...)
    {
        return opt;
    }
}
template<typename T, typename C,
         template<typename ...> class M, template<typename ...> class V>
detail::enable_if_t<
    detail::is_exact_toml_type<T, basic_value<C, M, V>>::value, T>&
get_or(basic_value<C, M, V>& v, T& opt)
{
    try
    {
        return get<detail::remove_cvref_t<T>>(v);
    }
    catch(...)
    {
        return opt;
    }
}
template<typename T, typename C,
         template<typename ...> class M, template<typename ...> class V>
detail::enable_if_t<detail::is_exact_toml_type<detail::remove_cvref_t<T>,
    basic_value<C, M, V>>::value, detail::remove_cvref_t<T>>
get_or(basic_value<C, M, V>&& v, T&& opt)
{
    try
    {
        return get<detail::remove_cvref_t<T>>(std::move(v));
    }
    catch(...)
    {
        return detail::remove_cvref_t<T>(std::forward<T>(opt));
    }
}

// ----------------------------------------------------------------------------
// specialization for std::string (return type becomes lvalue ref)

template<typename T, typename C,
         template<typename ...> class M, template<typename ...> class V>
detail::enable_if_t<std::is_same<detail::remove_cvref_t<T>, std::string>::value,
    std::string> const&
get_or(const basic_value<C, M, V>& v, const T& opt)
{
    try
    {
        return v.as_string().str;
    }
    catch(...)
    {
        return opt;
    }
}
template<typename T, typename C,
         template<typename ...> class M, template<typename ...> class V>
detail::enable_if_t<std::is_same<T, std::string>::value, std::string>&
get_or(basic_value<C, M, V>& v, T& opt)
{
    try
    {
        return v.as_string().str;
    }
    catch(...)
    {
        return opt;
    }
}
template<typename T, typename C,
         template<typename ...> class M, template<typename ...> class V>
detail::enable_if_t<
    std::is_same<detail::remove_cvref_t<T>, std::string>::value, std::string>
get_or(basic_value<C, M, V>&& v, T&& opt)
{
    try
    {
        return std::move(v.as_string().str);
    }
    catch(...)
    {
        return std::string(std::forward<T>(opt));
    }
}

// ----------------------------------------------------------------------------
// specialization for string literal

template<typename T, typename C,
         template<typename ...> class M, template<typename ...> class V>
detail::enable_if_t<detail::is_string_literal<
    typename std::remove_reference<T>::type>::value, std::string>
get_or(const basic_value<C, M, V>& v, T&& opt)
{
    try
    {
        return std::move(v.as_string().str);
    }
    catch(...)
    {
        return std::string(std::forward<T>(opt));
    }
}

// ----------------------------------------------------------------------------
// others (require type conversion and return type cannot be lvalue reference)

template<typename T, typename C,
         template<typename ...> class M, template<typename ...> class V>
detail::enable_if_t<detail::conjunction<
    detail::negation<detail::is_exact_toml_type<detail::remove_cvref_t<T>,
        basic_value<C, M, V>>>,
    detail::negation<std::is_same<std::string, detail::remove_cvref_t<T>>>,
    detail::negation<detail::is_string_literal<
        typename std::remove_reference<T>::type>>
    >::value, detail::remove_cvref_t<T>>
get_or(const basic_value<C, M, V>& v, T&& opt)
{
    try
    {
        return get<detail::remove_cvref_t<T>>(v);
    }
    catch(...)
    {
        return detail::remove_cvref_t<T>(std::forward<T>(opt));
    }
}

// ===========================================================================
// find_or(value, key, fallback)

template<typename C,
         template<typename ...> class M, template<typename ...> class V>
basic_value<C, M, V> const&
find_or(const basic_value<C, M, V>& v, const key& ky,
        const basic_value<C, M, V>& opt)
{
    if(!v.is_table()) {return opt;}
    const auto& tab = v.as_table();
    if(tab.count(ky) == 0) {return opt;}
    return tab.at(ky);
}

template<typename C,
         template<typename ...> class M, template<typename ...> class V>
basic_value<C, M, V>&
find_or(basic_value<C, M, V>& v, const toml::key& ky, basic_value<C, M, V>& opt)
{
    if(!v.is_table()) {return opt;}
    auto& tab = v.as_table();
    if(tab.count(ky) == 0) {return opt;}
    return tab.at(ky);
}

template<typename C,
         template<typename ...> class M, template<typename ...> class V>
basic_value<C, M, V>
find_or(basic_value<C, M, V>&& v, const toml::key& ky, basic_value<C, M, V>&& opt)
{
    if(!v.is_table()) {return opt;}
    auto tab = std::move(v).as_table();
    if(tab.count(ky) == 0) {return opt;}
    return basic_value<C, M, V>(std::move(tab.at(ky)));
}

// ---------------------------------------------------------------------------
// exact types (return type can be a reference)
template<typename T, typename C,
         template<typename ...> class M, template<typename ...> class V>
detail::enable_if_t<
    detail::is_exact_toml_type<T, basic_value<C, M, V>>::value, T> const&
find_or(const basic_value<C, M, V>& v, const key& ky, const T& opt)
{
    if(!v.is_table()) {return opt;}
    const auto& tab = v.as_table();
    if(tab.count(ky) == 0) {return opt;}
    return get_or(tab.at(ky), opt);
}

template<typename T, typename C,
         template<typename ...> class M, template<typename ...> class V>
detail::enable_if_t<
    detail::is_exact_toml_type<T, basic_value<C, M, V>>::value, T>&
find_or(basic_value<C, M, V>& v, const toml::key& ky, T& opt)
{
    if(!v.is_table()) {return opt;}
    auto& tab = v.as_table();
    if(tab.count(ky) == 0) {return opt;}
    return get_or(tab.at(ky), opt);
}

template<typename T, typename C,
         template<typename ...> class M, template<typename ...> class V>
detail::enable_if_t<
    detail::is_exact_toml_type<T, basic_value<C, M, V>>::value,
    detail::remove_cvref_t<T>>
find_or(basic_value<C, M, V>&& v, const toml::key& ky, T&& opt)
{
    if(!v.is_table()) {return std::forward<T>(opt);}
    auto tab = std::move(v).as_table();
    if(tab.count(ky) == 0) {return std::forward<T>(opt);}
    return get_or(std::move(tab.at(ky)), std::forward<T>(opt));
}

// ---------------------------------------------------------------------------
// std::string (return type can be a reference)

template<typename T, typename C,
         template<typename ...> class M, template<typename ...> class V>
detail::enable_if_t<std::is_same<T, std::string>::value, std::string> const&
find_or(const basic_value<C, M, V>& v, const key& ky, const T& opt)
{
    if(!v.is_table()) {return opt;}
    const auto& tab = v.as_table();
    if(tab.count(ky) == 0) {return opt;}
    return get_or(tab.at(ky), opt);
}
template<typename T, typename C,
         template<typename ...> class M, template<typename ...> class V>
detail::enable_if_t<std::is_same<T, std::string>::value, std::string>&
find_or(basic_value<C, M, V>& v, const toml::key& ky, T& opt)
{
    if(!v.is_table()) {return opt;}
    auto& tab = v.as_table();
    if(tab.count(ky) == 0) {return opt;}
    return get_or(tab.at(ky), opt);
}
template<typename T, typename C,
         template<typename ...> class M, template<typename ...> class V>
detail::enable_if_t<std::is_same<T, std::string>::value, std::string>
find_or(basic_value<C, M, V>&& v, const toml::key& ky, T&& opt)
{
    if(!v.is_table()) {return std::forward<T>(opt);}
    auto tab = std::move(v).as_table();
    if(tab.count(ky) == 0) {return std::forward<T>(opt);}
    return get_or(std::move(tab.at(ky)), std::forward<T>(opt));
}

// ---------------------------------------------------------------------------
// string literal (deduced as std::string)
template<typename T, typename C,
         template<typename ...> class M, template<typename ...> class V>
detail::enable_if_t<
    detail::is_string_literal<typename std::remove_reference<T>::type>::value,
    std::string>
find_or(const basic_value<C, M, V>& v, const toml::key& ky, T&& opt)
{
    if(!v.is_table()) {return std::string(opt);}
    const auto& tab = v.as_table();
    if(tab.count(ky) == 0) {return std::string(opt);}
    return get_or(tab.at(ky), std::forward<T>(opt));
}

// ---------------------------------------------------------------------------
// others (require type conversion and return type cannot be lvalue reference)
template<typename T, typename C,
         template<typename ...> class M, template<typename ...> class V>
detail::enable_if_t<detail::conjunction<
    // T is not an exact toml type
    detail::negation<detail::is_exact_toml_type<
        detail::remove_cvref_t<T>, basic_value<C, M, V>>>,
    // T is not std::string
    detail::negation<std::is_same<std::string, detail::remove_cvref_t<T>>>,
    // T is not a string literal
    detail::negation<detail::is_string_literal<
        typename std::remove_reference<T>::type>>
    >::value, detail::remove_cvref_t<T>>
find_or(const basic_value<C, M, V>& v, const toml::key& ky, T&& opt)
{
    if(!v.is_table()) {return std::forward<T>(opt);}
    const auto& tab = v.as_table();
    if(tab.count(ky) == 0) {return std::forward<T>(opt);}
    return get_or(tab.at(ky), std::forward<T>(opt));
}

// ---------------------------------------------------------------------------
// recursive find-or with type deduction (find_or(value, keys, opt))

template<typename Value, typename ... Ks,
         typename detail::enable_if_t<(sizeof...(Ks) > 1), std::nullptr_t> = nullptr>
         // here we need to add SFINAE in the template parameter to avoid
         // infinite recursion in type deduction on gcc
auto find_or(Value&& v, const toml::key& ky, Ks&& ... keys)
    -> decltype(find_or(std::forward<Value>(v), ky, detail::last_one(std::forward<Ks>(keys)...)))
{
    if(!v.is_table())
    {
        return detail::last_one(std::forward<Ks>(keys)...);
    }
    auto&& tab = std::forward<Value>(v).as_table();
    if(tab.count(ky) == 0)
    {
        return detail::last_one(std::forward<Ks>(keys)...);
    }
    return find_or(std::forward<decltype(tab)>(tab).at(ky), std::forward<Ks>(keys)...);
}

// ---------------------------------------------------------------------------
// recursive find_or with explicit type specialization, find_or<int>(value, keys...)

template<typename T, typename Value, typename ... Ks,
         typename detail::enable_if_t<(sizeof...(Ks) > 1), std::nullptr_t> = nullptr>
         // here we need to add SFINAE in the template parameter to avoid
         // infinite recursion in type deduction on gcc
auto find_or(Value&& v, const toml::key& ky, Ks&& ... keys)
    -> decltype(find_or<T>(std::forward<Value>(v), ky, detail::last_one(std::forward<Ks>(keys)...)))
{
    if(!v.is_table())
    {
        return detail::last_one(std::forward<Ks>(keys)...);
    }
    auto&& tab = std::forward<Value>(v).as_table();
    if(tab.count(ky) == 0)
    {
        return detail::last_one(std::forward<Ks>(keys)...);
    }
    return find_or(std::forward<decltype(tab)>(tab).at(ky), std::forward<Ks>(keys)...);
}

// ============================================================================
// expect

template<typename T, typename C,
         template<typename ...> class M, template<typename ...> class V>
result<T, std::string> expect(const basic_value<C, M, V>& v) noexcept
{
    try
    {
        return ok(get<T>(v));
    }
    catch(const std::exception& e)
    {
        return err(e.what());
    }
}
template<typename T, typename C,
         template<typename ...> class M, template<typename ...> class V>
result<T, std::string>
expect(const basic_value<C, M, V>& v, const toml::key& k) noexcept
{
    try
    {
        return ok(find<T>(v, k));
    }
    catch(const std::exception& e)
    {
        return err(e.what());
    }
}

} // toml
#endif// TOML11_GET
