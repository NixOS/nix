#pragma once
///@file

#define DECLARE_ONE_CMP(PRE, QUAL, COMPARATOR, MY_TYPE) \
    PRE bool QUAL operator COMPARATOR(const MY_TYPE & other) const;
#define DECLARE_EQUAL(prefix, qualification, my_type) \
    DECLARE_ONE_CMP(prefix, qualification, ==, my_type)
#define DECLARE_LEQ(prefix, qualification, my_type) \
    DECLARE_ONE_CMP(prefix, qualification, <, my_type)
#define DECLARE_NEQ(prefix, qualification, my_type) \
    DECLARE_ONE_CMP(prefix, qualification, !=, my_type)

#define GENERATE_ONE_CMP(PRE, QUAL, COMPARATOR, MY_TYPE, ...) \
    PRE bool QUAL operator COMPARATOR(const MY_TYPE & other) const { \
      __VA_OPT__(const MY_TYPE * me = this;) \
      auto fields1 = std::tie( __VA_ARGS__ ); \
      __VA_OPT__(me = &other;) \
      auto fields2 = std::tie( __VA_ARGS__ ); \
      return fields1 COMPARATOR fields2; \
    }
#define GENERATE_EQUAL(prefix, qualification, my_type, args...) \
    GENERATE_ONE_CMP(prefix, qualification, ==, my_type, args)
#define GENERATE_LEQ(prefix, qualification, my_type, args...) \
    GENERATE_ONE_CMP(prefix, qualification, <, my_type, args)
#define GENERATE_NEQ(prefix, qualification, my_type, args...) \
    GENERATE_ONE_CMP(prefix, qualification, !=, my_type, args)

/**
 * Declare comparison methods without defining them.
 */
#define DECLARE_CMP(my_type) \
    DECLARE_EQUAL(,,my_type) \
    DECLARE_LEQ(,,my_type) \
    DECLARE_NEQ(,,my_type)

/**
 * @param prefix This is for something before each declaration like
 * `template<classname Foo>`.
 *
 * @param my_type the type are defining operators for.
 */
#define DECLARE_CMP_EXT(prefix, qualification, my_type) \
    DECLARE_EQUAL(prefix, qualification, my_type) \
    DECLARE_LEQ(prefix, qualification, my_type) \
    DECLARE_NEQ(prefix, qualification, my_type)

/**
 * Awful hacky generation of the comparison operators by doing a lexicographic
 * comparison between the choosen fields.
 *
 * ```
 * GENERATE_CMP(ClassName, me->field1, me->field2, ...)
 * ```
 *
 * will generate comparison operators semantically equivalent to:
 *
 * ```
 * bool operator<(const ClassName& other) {
 *   return field1 < other.field1 && field2 < other.field2 && ...;
 * }
 * ```
 */
#define GENERATE_CMP(args...) \
    GENERATE_EQUAL(,,args) \
    GENERATE_LEQ(,,args) \
    GENERATE_NEQ(,,args)

/**
 * @param prefix This is for something before each declaration like
 * `template<classname Foo>`.
 *
 * @param my_type the type are defining operators for.
 */
#define GENERATE_CMP_EXT(prefix, my_type, args...) \
    GENERATE_EQUAL(prefix, my_type ::, my_type, args) \
    GENERATE_LEQ(prefix, my_type ::, my_type, args) \
    GENERATE_NEQ(prefix, my_type ::, my_type, args)
