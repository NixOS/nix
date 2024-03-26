#ifndef TOML11_VERSION_HPP
#define TOML11_VERSION_HPP

// This file checks C++ version.

#ifndef __cplusplus
#    error "__cplusplus is not defined"
#endif

// Since MSVC does not define `__cplusplus` correctly unless you pass
// `/Zc:__cplusplus` when compiling, the workaround macros are added.
// Those enables you to define version manually or to use MSVC specific
// version macro automatically.
//
// The value of `__cplusplus` macro is defined in the C++ standard spec, but
// MSVC ignores the value, maybe because of backward compatibility. Instead,
// MSVC defines _MSVC_LANG that has the same value as __cplusplus defined in
// the C++ standard. First we check the manual version definition, and then
// we check if _MSVC_LANG is defined. If neither, use normal `__cplusplus`.
//
// FYI: https://docs.microsoft.com/en-us/cpp/build/reference/zc-cplusplus?view=msvc-170
//      https://docs.microsoft.com/en-us/cpp/preprocessor/predefined-macros?view=msvc-170
//
#if   defined(TOML11_ENFORCE_CXX11)
#  define TOML11_CPLUSPLUS_STANDARD_VERSION 201103L
#elif defined(TOML11_ENFORCE_CXX14)
#  define TOML11_CPLUSPLUS_STANDARD_VERSION 201402L
#elif defined(TOML11_ENFORCE_CXX17)
#  define TOML11_CPLUSPLUS_STANDARD_VERSION 201703L
#elif defined(TOML11_ENFORCE_CXX20)
#  define TOML11_CPLUSPLUS_STANDARD_VERSION 202002L
#elif defined(_MSVC_LANG) && defined(_MSC_VER) && 1910 <= _MSC_VER
#  define TOML11_CPLUSPLUS_STANDARD_VERSION _MSVC_LANG
#else
#  define TOML11_CPLUSPLUS_STANDARD_VERSION __cplusplus
#endif

#if TOML11_CPLUSPLUS_STANDARD_VERSION < 201103L && _MSC_VER < 1900
#    error "toml11 requires C++11 or later."
#endif

#endif// TOML11_VERSION_HPP
