#include <gtest/gtest.h>

#include "sort.hh"

#include <vector>
#include <list>
#include <algorithm>
#include <random>

namespace nix {

struct MonotonicSubranges : public ::testing::Test
{
    std::vector<int> empty_;
    std::vector<int> basic_ = {1, 0, -1, -100, 10, 10, 20, 40, 5, 5, 20, 10, 10, 1, -5};
};

TEST_F(MonotonicSubranges, empty)
{
    ASSERT_EQ(weaklyIncreasingPrefix(empty_.begin(), empty_.end()), empty_.begin());
    ASSERT_EQ(weaklyIncreasingSuffix(empty_.begin(), empty_.end()), empty_.begin());
    ASSERT_EQ(strictlyDecreasingPrefix(empty_.begin(), empty_.end()), empty_.begin());
    ASSERT_EQ(strictlyDecreasingSuffix(empty_.begin(), empty_.end()), empty_.begin());
}

TEST_F(MonotonicSubranges, basic)
{
    ASSERT_EQ(strictlyDecreasingPrefix(basic_.begin(), basic_.end()), basic_.begin() + 4);
    ASSERT_EQ(strictlyDecreasingSuffix(basic_.begin(), basic_.end()), basic_.begin() + 12);
    std::reverse(basic_.begin(), basic_.end());
    ASSERT_EQ(weaklyIncreasingPrefix(basic_.begin(), basic_.end()), basic_.begin() + 5);
    ASSERT_EQ(weaklyIncreasingSuffix(basic_.begin(), basic_.end()), basic_.begin() + 11);
}

template<typename T>
class SortTestPermutations : public ::testing::Test
{
    std::vector<T> vector_ = {std::numeric_limits<T>::max(), std::numeric_limits<T>::min(), 0, 0, 42, 126, 36};
    std::list<T> list_;

public:
    std::vector<T> scratchVector_ = vector_;
    std::list<T> scratchList_ = list_;
    std::vector<T> empty_;

    void SetUp() override
    {
        std::sort(vector_.begin(), vector_.end());
        list_ = std::list<T>(vector_.begin(), vector_.end());
    }

    bool nextPermutation()
    {
        std::next_permutation(vector_.begin(), vector_.end());
        std::next_permutation(list_.begin(), list_.end());
        scratchList_ = list_;
        scratchVector_ = vector_;
        return std::is_sorted(vector_.begin(), vector_.end());
    }
};

using SelectionSortPermutationsTypes = ::testing::Types<int, long long, short, unsigned, unsigned long>;

TYPED_TEST_SUITE(SortTestPermutations, SelectionSortPermutationsTypes);

TYPED_TEST(SortTestPermutations, insertionsort)
{
    while (!this->nextPermutation()) {
        auto & list = this->scratchList_;
        insertionsort(list.begin(), list.end());
        ASSERT_TRUE(std::is_sorted(list.begin(), list.end()));
        auto & vector = this->scratchVector_;
        insertionsort(vector.begin(), vector.end());
        ASSERT_TRUE(std::is_sorted(vector.begin(), vector.end()));
    }
}

TYPED_TEST(SortTestPermutations, peeksort)
{
    while (!this->nextPermutation()) {
        auto & vector = this->scratchVector_;
        peeksort(vector.begin(), vector.end());
        ASSERT_TRUE(std::is_sorted(vector.begin(), vector.end()));
    }
}

TEST(InsertionSort, empty)
{
    std::vector<int> empty;
    insertionsort(empty.begin(), empty.end());
}

struct RandomPeekSort : public ::testing::TestWithParam<
                            std::tuple</*maxSize*/ std::size_t, /*min*/ int, /*max*/ int, /*iterations*/ std::size_t>>
{
    using ValueType = int;
    std::vector<ValueType> data_;
    std::mt19937 urng_;
    std::uniform_int_distribution<int> distribution_;

    void SetUp() override
    {
        auto [maxSize, min, max, iterations] = GetParam();
        urng_ = std::mt19937(GTEST_FLAG_GET(random_seed));
        distribution_ = std::uniform_int_distribution<int>(min, max);
    }

    auto regenerate()
    {
        auto [maxSize, min, max, iterations] = GetParam();
        std::size_t dataSize = std::uniform_int_distribution<std::size_t>(0, maxSize)(urng_);
        data_.resize(dataSize);
        std::generate(data_.begin(), data_.end(), [&]() { return distribution_(urng_); });
    }
};

TEST_P(RandomPeekSort, defaultComparator)
{
    auto [maxSize, min, max, iterations] = GetParam();

    for (std::size_t i = 0; i < iterations; ++i) {
        regenerate();
        peeksort(data_.begin(), data_.end());
        ASSERT_TRUE(std::is_sorted(data_.begin(), data_.end()));
        /* Sorting is idempotent */
        peeksort(data_.begin(), data_.end());
        ASSERT_TRUE(std::is_sorted(data_.begin(), data_.end()));
    }
}

TEST_P(RandomPeekSort, greater)
{
    auto [maxSize, min, max, iterations] = GetParam();

    for (std::size_t i = 0; i < iterations; ++i) {
        regenerate();
        peeksort(data_.begin(), data_.end(), std::greater<int>{});
        ASSERT_TRUE(std::is_sorted(data_.begin(), data_.end(), std::greater<int>{}));
        /* Sorting is idempotent */
        peeksort(data_.begin(), data_.end(), std::greater<int>{});
        ASSERT_TRUE(std::is_sorted(data_.begin(), data_.end(), std::greater<int>{}));
    }
}

TEST_P(RandomPeekSort, brokenComparator)
{
    auto [maxSize, min, max, iterations] = GetParam();

    /* This is a pretty nice way of modeling a worst-case scenario for a broken comparator.
       If the sorting algorithm doesn't break in such case, then surely all deterministic
       predicates won't break it. */
    auto comp = [&]([[maybe_unused]] const auto & lhs, [[maybe_unused]] const auto & rhs) -> bool {
        return std::uniform_int_distribution<unsigned>(0, 1)(urng_);
    };

    for (std::size_t i = 0; i < iterations; ++i) {
        regenerate();
        auto originalData = data_;
        peeksort(data_.begin(), data_.end(), comp);
        /* Check that the output is just a reordering of the input. This is the
           contract of the implementation in regard to comparators that don't
           define a strict weak order. */
        std::sort(data_.begin(), data_.end());
        std::sort(originalData.begin(), originalData.end());
        ASSERT_EQ(originalData, data_);
    }
}

TEST_P(RandomPeekSort, stability)
{
    auto [maxSize, min, max, iterations] = GetParam();

    for (std::size_t i = 0; i < iterations; ++i) {
        regenerate();
        std::vector<std::pair<int, int>> pairs;

        /* Assign sequential ids to objects. After the sort ids for equivalent
           elements should be in ascending order. */
        std::transform(
            data_.begin(), data_.end(), std::back_inserter(pairs), [id = std::size_t{0}](auto && val) mutable {
                return std::pair{val, ++id};
            });

        auto comp = [&]([[maybe_unused]] const auto & lhs, [[maybe_unused]] const auto & rhs) -> bool {
            return lhs.first > rhs.first;
        };

        peeksort(pairs.begin(), pairs.end(), comp);
        ASSERT_TRUE(std::is_sorted(pairs.begin(), pairs.end(), comp));

        for (auto begin = pairs.begin(), end = pairs.end(); begin < end; ++begin) {
            auto key = begin->first;
            auto prevId = begin->second;
            auto innerEnd = std::find_if_not(begin, end, [key](const auto & lhs) { return lhs.first == key; });

            for (auto innerBegin = begin; innerBegin < innerEnd; ++innerBegin) {
                ASSERT_LE(prevId, innerBegin->second);
                prevId = innerBegin->second;
            }

            begin = innerEnd;
        }
    }
}

using RandomPeekSortParamType = RandomPeekSort::ParamType;

INSTANTIATE_TEST_SUITE_P(
    PeekSort,
    RandomPeekSort,
    ::testing::Values(
        RandomPeekSortParamType{128, std::numeric_limits<int>::min(), std::numeric_limits<int>::max(), 1024},
        RandomPeekSortParamType{7753, -32, 32, 128},
        RandomPeekSortParamType{11719, std::numeric_limits<int>::min(), std::numeric_limits<int>::max(), 64},
        RandomPeekSortParamType{4063, 0, 32, 256},
        RandomPeekSortParamType{771, -8, 8, 2048},
        RandomPeekSortParamType{433, 0, 1, 2048},
        RandomPeekSortParamType{0, 0, 0, 1}, /* empty case */
        RandomPeekSortParamType{
            1, std::numeric_limits<int>::min(), std::numeric_limits<int>::max(), 1}, /* single element */
        RandomPeekSortParamType{
            2, std::numeric_limits<int>::min(), std::numeric_limits<int>::max(), 2}, /* two elements */
        RandomPeekSortParamType{55425, std::numeric_limits<int>::min(), std::numeric_limits<int>::max(), 128}));

} // namespace nix
