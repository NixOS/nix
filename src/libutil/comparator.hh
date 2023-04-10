#pragma once
///@file

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
#define GENERATE_ONE_CMP(COMPARATOR, MY_TYPE, ...) \
    bool operator COMPARATOR(const MY_TYPE& other) const { \
      __VA_OPT__(const MY_TYPE* me = this;) \
      auto fields1 = std::make_tuple( __VA_ARGS__ ); \
      __VA_OPT__(me = &other;) \
      auto fields2 = std::make_tuple( __VA_ARGS__ ); \
      return fields1 COMPARATOR fields2; \
    }
#define GENERATE_EQUAL(args...) GENERATE_ONE_CMP(==, args)
#define GENERATE_LEQ(args...) GENERATE_ONE_CMP(<, args)
#define GENERATE_NEQ(args...) GENERATE_ONE_CMP(!=, args)
#define GENERATE_CMP(args...) \
    GENERATE_EQUAL(args) \
    GENERATE_LEQ(args) \
    GENERATE_NEQ(args)
