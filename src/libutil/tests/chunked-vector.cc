#include "chunked-vector.hh"

#include <gtest/gtest.h>

namespace nix {
    TEST(ChunkedVector, InitEmpty) {
        auto v = ChunkedVector<int, 2>(100);
        ASSERT_EQ(v.size(), 0);
    }

    TEST(ChunkedVector, GrowsCorrectly) {
        auto v = ChunkedVector<int, 2>(100);
        for (auto i = 1; i < 20; i++) {
            v.add(i);
            ASSERT_EQ(v.size(), i);
        }
    }

    TEST(ChunkedVector, AddAndGet) {
        auto v = ChunkedVector<int, 2>(100);
        for (auto i = 1; i < 20; i++) {
            auto [i2, idx] = v.add(i);
            auto & i3 = v[idx];
            ASSERT_EQ(i, i2);
            ASSERT_EQ(&i2, &i3);
        }
    }

    TEST(ChunkedVector, ForEach) {
        auto v = ChunkedVector<int, 2>(100);
        for (auto i = 1; i < 20; i++) {
            v.add(i);
        }
        int count = 0;
        v.forEach([&count](int elt) {
            count++;
        });
        ASSERT_EQ(count, v.size());
    }

    TEST(ChunkedVector, OverflowOK) {
        // Similar to the AddAndGet, but intentionnally use a small
        // initial ChunkedVector to force it to overflow
        auto v = ChunkedVector<int, 2>(2);
        for (auto i = 1; i < 20; i++) {
            auto [i2, idx] = v.add(i);
            auto & i3 = v[idx];
            ASSERT_EQ(i, i2);
            ASSERT_EQ(&i2, &i3);
        }
    }

}

