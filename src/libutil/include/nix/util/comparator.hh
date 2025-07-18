#pragma once
///@file

#define GENERATE_ONE_CMP(PRE, RET, QUAL, COMPARATOR, MY_TYPE, ...)         \
    PRE RET QUAL operator COMPARATOR(const MY_TYPE & other) const noexcept \
    {                                                                      \
        __VA_OPT__(const MY_TYPE * me = this;)                             \
        auto fields1 = std::tie(__VA_ARGS__);                              \
        __VA_OPT__(me = &other;)                                           \
        auto fields2 = std::tie(__VA_ARGS__);                              \
        return fields1 COMPARATOR fields2;                                 \
    }
#define GENERATE_EQUAL(prefix, qualification, my_type, args...) \
    GENERATE_ONE_CMP(prefix, bool, qualification, ==, my_type, args)
#define GENERATE_SPACESHIP(prefix, ret, qualification, my_type, args...) \
    GENERATE_ONE_CMP(prefix, ret, qualification, <=>, my_type, args)

/**
 * Awful hacky generation of the comparison operators by doing a lexicographic
 * comparison between the chosen fields.
 *
 * ```
 * GENERATE_CMP(ClassName, me->field1, me->field2, ...)
 * ```
 *
 * will generate comparison operators semantically equivalent to:
 *
 * ```
 * auto operator<=>(const ClassName& other) const noexcept {
 *   if (auto cmp = field1 <=> other.field1; cmp != 0)
 *      return cmp;
 *   if (auto cmp = field2 <=> other.field2; cmp != 0)
 *      return cmp;
 *   ...
 *   return 0;
 * }
 * ```
 */
#define GENERATE_CMP(args...) \
    GENERATE_EQUAL(, , args)  \
    GENERATE_SPACESHIP(, auto, , args)

/**
 * @param prefix This is for something before each declaration like
 * `template<classname Foo>`.
 *
 * @param my_type the type are defining operators for.
 */
#define GENERATE_CMP_EXT(prefix, ret, my_type, args...) \
    GENERATE_EQUAL(prefix, my_type ::, my_type, args)   \
    GENERATE_SPACESHIP(prefix, ret, my_type ::, my_type, args)
