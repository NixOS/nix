#pragma once

/* Awfull hacky generation of the comparison operators by doing a lexicographic
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
#define GENERATE_ONE_CMP(COMPARATOR, MY_TYPE, FIELDS...) \
    bool operator COMPARATOR(const MY_TYPE& other) const { \
      const MY_TYPE* me = this; \
      auto fields1 = std::make_tuple( FIELDS ); \
      me = &other; \
      auto fields2 = std::make_tuple( FIELDS ); \
      return fields1 COMPARATOR fields2; \
    }
#define GENERATE_EQUAL(args...) GENERATE_ONE_CMP(==, args)
#define GENERATE_LEQ(args...) GENERATE_ONE_CMP(<, args)
#define GENERATE_NEQ(args...) GENERATE_ONE_CMP(!=, args)
#define GENERATE_CMP(args...) \
    GENERATE_EQUAL(args) \
    GENERATE_LEQ(args) \
    GENERATE_NEQ(args)
