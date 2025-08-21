#pragma once
/**
 * @file
 *
 * Template machinery useful for configuration classes.
 *
 * The use case for these is higher-order templates:
 *
 * ```
 * template<template<typename> class F>
 * struct Foo
 * {
 *   F<int>::value foo;
 *   F<bool>::value bar;
 * };
 * ```
 *
 * One could use e.g. a `Foo<PlainValue>`, which is isomorphic to
 *
 * ```
 * struct PlainFoo
 * {
 *   int foo;
 *   bool bar;
 * };
 * ```
 *
 * Or one could use e.g. a `Foo<OptionalValue>`, which is isomorphic to
 *
 * ```
 * struct FooOfOptionals
 * {
 *   std::optional<int> foo;
 *   std::optional<bool> bar;
 * };
 * ```
 */

#include <optional>

namespace nix::config {

/**
 * (Encoding of the) `T -> T` identity type function
 *
 * You "call" the function like `PlainValue<Arg>::type`, which is equal
 * to just `Arg`.
 */
template<typename T>
struct PlainValue
{
    using type = T;
};

/**
 * (Encoding of the) `T -> std::optional<T>` type function
 *
 * The idea is that `OptionalValue<T>::type = T(*)()`.
 *
 * You "call" the function like `OptionalValue<Arg>::type`, which is
 * equal to just `std::optional<Arg>`.
 *
 * The use case for this is higher-order templates:
 * ```
 * template<template<typename> class F>
 * class Foo
 * {
 *   F<int>::value foo;
 *   F<bool>::value bar;
 * };
 * ```
 */
template<typename T>
struct OptionalValue
{
    using type = std::optional<T>;
};

} // namespace nix::config
