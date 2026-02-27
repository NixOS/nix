#pragma once
///@file

// not used, but will be used by callers
#include <variant>

#define FORCE_DEFAULT_MOVE_CONSTRUCTORS(CLASS_NAME) \
    CLASS_NAME(CLASS_NAME &&) = default;            \
    CLASS_NAME & operator=(CLASS_NAME &&) = default;

/**
 * Force the default versions of all constructors (copy, move, copy
 * assignment).
 */
#define FORCE_DEFAULT_CONSTRUCTORS(CLASS_NAME)            \
    FORCE_DEFAULT_MOVE_CONSTRUCTORS(CLASS_NAME)           \
                                                          \
    CLASS_NAME(const CLASS_NAME &) = default;             \
    CLASS_NAME(CLASS_NAME &) = default;                   \
                                                          \
    CLASS_NAME & operator=(const CLASS_NAME &) = default; \
    CLASS_NAME & operator=(CLASS_NAME &) = default;

/**
 * Forwarding constructor for wrapper types. All args are forwarded to
 * the construction of the "raw" field.
 *
 * The moral equivalent of `using Raw::Raw;`
 */
#define MAKE_WRAPPER_CONSTRUCTOR_RAW(CLASS_NAME)                                                            \
    template<typename... Args>                                                                              \
        requires(!(sizeof...(Args) == 1 && (std::is_same_v<std::remove_cvref_t<Args>, CLASS_NAME> && ...))) \
    CLASS_NAME(Args &&... arg)                                                                              \
        : raw(std::forward<Args>(arg)...)                                                                   \
    {                                                                                                       \
    }

/**
 * Make a wrapper constructor. Also defaults move constructor/assignment.
 * Copy operations will be implicitly defaulted or deleted based on the
 * Raw type.
 */
#define MAKE_WRAPPER_CONSTRUCTOR_MOVE_ONLY(CLASS_NAME) \
    FORCE_DEFAULT_MOVE_CONSTRUCTORS(CLASS_NAME)        \
    MAKE_WRAPPER_CONSTRUCTOR_RAW(CLASS_NAME)

/**
 * Like MAKE_WRAPPER_CONSTRUCTOR, but also explicitly defaults copy
 * operations for copyable types.
 */
#define MAKE_WRAPPER_CONSTRUCTOR(CLASS_NAME) \
    FORCE_DEFAULT_CONSTRUCTORS(CLASS_NAME)   \
    MAKE_WRAPPER_CONSTRUCTOR_RAW(CLASS_NAME)
