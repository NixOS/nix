#pragma once
///@file

#include <gtest/gtest.h>

/**
 * `EXPECT_EQ` in both argument orders.
 *
 * A comparison operator can treat its operands differently, for instance
 * as a member function that runs on the left operand.
 */
#define EXPECT_EQ_SYMMETRICALLY(a, b) \
    do {                              \
        EXPECT_EQ(a, b);              \
        EXPECT_EQ(b, a);              \
    } while (0)

/**
 * `EXPECT_NE` in both argument orders. See `EXPECT_EQ_SYMMETRICALLY`.
 */
#define EXPECT_NE_SYMMETRICALLY(a, b) \
    do {                              \
        EXPECT_NE(a, b);              \
        EXPECT_NE(b, a);              \
    } while (0)
