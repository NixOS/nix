#include "suggestions.hh"
#include <gtest/gtest.h>

namespace nix {

    struct LevenshteinDistanceParam {
        std::string s1, s2;
        int distance;
    };

    class LevenshteinDistanceTest :
        public testing::TestWithParam<LevenshteinDistanceParam> {
    };

    TEST_P(LevenshteinDistanceTest, CorrectlyComputed) {
        auto params = GetParam();

        ASSERT_EQ(levenshteinDistance(params.s1, params.s2), params.distance);
        ASSERT_EQ(levenshteinDistance(params.s2, params.s1), params.distance);
    }

    INSTANTIATE_TEST_SUITE_P(LevenshteinDistance, LevenshteinDistanceTest,
            testing::Values(
                LevenshteinDistanceParam{"foo", "foo", 0},
                LevenshteinDistanceParam{"foo", "", 3},
                LevenshteinDistanceParam{"", "", 0},
                LevenshteinDistanceParam{"foo", "fo", 1},
                LevenshteinDistanceParam{"foo", "oo", 1},
                LevenshteinDistanceParam{"foo", "fao", 1},
                LevenshteinDistanceParam{"foo", "abc", 3}
            )
    );

    TEST(Suggestions, Trim) {
        auto suggestions = Suggestions::bestMatches({"foooo", "bar", "fo", "gao"}, "foo");
        auto onlyOne = suggestions.trim(1);
        ASSERT_EQ(onlyOne.suggestions.size(), 1);
        ASSERT_TRUE(onlyOne.suggestions.begin()->suggestion == "fo");

        auto closest = suggestions.trim(999, 2);
        ASSERT_EQ(closest.suggestions.size(), 3);
    }
}
