#pragma once
///@file

// not used, but will be used by callers
#include <variant>

/**
 * Force the default versions of all constructors (copy, move, copy
 * assignment).
 */
#define FORCE_DEFAULT_CONSTRUCTORS(CLASS_NAME) \
    CLASS_NAME(const CLASS_NAME &) = default; \
    CLASS_NAME(CLASS_NAME &) = default; \
    CLASS_NAME(CLASS_NAME &&) = default; \
    \
    CLASS_NAME & operator =(const CLASS_NAME &) = default; \
    CLASS_NAME & operator =(CLASS_NAME &) = default;

/**
 * Make a wrapper constructor. All args are forwarded to the
 * construction of the "raw" field. (Which we assume is the only one.)
 *
 * The moral equivalent of `using Raw::Raw;`
 */
#define MAKE_WRAPPER_CONSTRUCTOR(CLASS_NAME) \
    FORCE_DEFAULT_CONSTRUCTORS(CLASS_NAME) \
    \
    CLASS_NAME(auto &&... arg) \
        : raw(std::forward<decltype(arg)>(arg)...) \
    { }
