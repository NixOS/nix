#pragma once
///@file

#include <string>
#include <vector>
#include <set>
#include <map>
#include <list>

namespace nix {

/**
 * For `adl_serializer<std::optional<T>>` below, we need to track what
 * types are not already using `null`. Only for them can we use `null`
 * to represent `std::nullopt`.
 */
template<typename T>
struct json_avoids_null;

/**
 * Handle numbers in default impl
 */
template<typename T>
struct json_avoids_null : std::bool_constant<std::is_integral<T>::value>
{};

template<>
struct json_avoids_null<std::nullptr_t> : std::false_type
{};

template<>
struct json_avoids_null<bool> : std::true_type
{};

template<>
struct json_avoids_null<std::string> : std::true_type
{};

template<typename T>
struct json_avoids_null<std::vector<T>> : std::true_type
{};

template<typename T>
struct json_avoids_null<std::list<T>> : std::true_type
{};

template<typename T, typename Compare>
struct json_avoids_null<std::set<T, Compare>> : std::true_type
{};

template<typename K, typename V, typename Compare>
struct json_avoids_null<std::map<K, V, Compare>> : std::true_type
{};

} // namespace nix
