#include <gtest/gtest.h>
#include <rapidcheck/gtest.h>
#include "nix/util/sort.hh"

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
    std::vector<T> initialData = {std::numeric_limits<T>::max(), std::numeric_limits<T>::min(), 0, 0, 42, 126, 36};
    std::vector<T> vectorData;
    std::list<T> listData;

public:
    std::vector<T> scratchVector;
    std::list<T> scratchList;
    std::vector<T> empty;

    void SetUp() override
    {
        vectorData = initialData;
        std::sort(vectorData.begin(), vectorData.end());
        listData = std::list(vectorData.begin(), vectorData.end());
    }

    bool nextPermutation()
    {
        std::next_permutation(vectorData.begin(), vectorData.end());
        std::next_permutation(listData.begin(), listData.end());
        scratchList = listData;
        scratchVector = vectorData;
        return vectorData == initialData;
    }
};

using SortPermutationsTypes = ::testing::Types<int, long long, short, unsigned, unsigned long>;

TYPED_TEST_SUITE(SortTestPermutations, SortPermutationsTypes);

TYPED_TEST(SortTestPermutations, insertionsort)
{
    while (!this->nextPermutation()) {
        auto & list = this->scratchList;
        insertionsort(list.begin(), list.end());
        ASSERT_TRUE(std::is_sorted(list.begin(), list.end()));
        auto & vector = this->scratchVector;
        insertionsort(vector.begin(), vector.end());
        ASSERT_TRUE(std::is_sorted(vector.begin(), vector.end()));
    }
}

TYPED_TEST(SortTestPermutations, peeksort)
{
    while (!this->nextPermutation()) {
        auto & vector = this->scratchVector;
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
        const auto & [maxSize, min, max, iterations] = GetParam();
        urng_ = std::mt19937(GTEST_FLAG_GET(random_seed));
        distribution_ = std::uniform_int_distribution<int>(min, max);
    }

    auto regenerate()
    {
        const auto & [maxSize, min, max, iterations] = GetParam();
        std::size_t dataSize = std::uniform_int_distribution<std::size_t>(0, maxSize)(urng_);
        data_.resize(dataSize);
        std::generate(data_.begin(), data_.end(), [&]() { return distribution_(urng_); });
    }
};

TEST_P(RandomPeekSort, defaultComparator)
{
    const auto & [maxSize, min, max, iterations] = GetParam();

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
    const auto & [maxSize, min, max, iterations] = GetParam();

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
    const auto & [maxSize, min, max, iterations] = GetParam();

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
    const auto & [maxSize, min, max, iterations] = GetParam();

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
            auto innerEnd = std::find_if_not(begin, end, [key](const auto & lhs) { return lhs.first == key; });
            ASSERT_TRUE(std::is_sorted(begin, innerEnd, [](const auto & lhs, const auto & rhs) {
                return lhs.second < rhs.second;
            }));
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

template<typename T>
struct SortProperty : public ::testing::Test
{};

using SortPropertyTypes = ::testing::Types<int, unsigned, long long, short, std::string>;
TYPED_TEST_SUITE(SortProperty, SortPropertyTypes);

RC_GTEST_TYPED_FIXTURE_PROP(SortProperty, peeksortSorted, (std::vector<TypeParam> vec))
{
    peeksort(vec.begin(), vec.end());
    RC_ASSERT(std::is_sorted(vec.begin(), vec.end()));
}

RC_GTEST_TYPED_FIXTURE_PROP(SortProperty, peeksortSortedGreater, (std::vector<TypeParam> vec))
{
    auto comp = std::greater<TypeParam>();
    peeksort(vec.begin(), vec.end(), comp);
    RC_ASSERT(std::is_sorted(vec.begin(), vec.end(), comp));
}

RC_GTEST_TYPED_FIXTURE_PROP(SortProperty, insertionsortSorted, (std::vector<TypeParam> vec))
{
    insertionsort(vec.begin(), vec.end());
    RC_ASSERT(std::is_sorted(vec.begin(), vec.end()));
}

RC_GTEST_PROP(SortProperty, peeksortStability, (std::vector<std::pair<char, char>> vec))
{
    auto comp = [](auto lhs, auto rhs) { return lhs.first < rhs.first; };
    auto copy = vec;
    std::stable_sort(copy.begin(), copy.end(), comp);
    peeksort(vec.begin(), vec.end(), comp);
    RC_ASSERT(copy == vec);
}

RC_GTEST_TYPED_FIXTURE_PROP(SortProperty, peeksortSortedLinearComparisonComplexity, (std::vector<TypeParam> vec))
{
    peeksort(vec.begin(), vec.end());
    RC_ASSERT(std::is_sorted(vec.begin(), vec.end()));
    std::size_t comparisonCount = 0;
    auto countingComp = [&](auto lhs, auto rhs) {
        ++comparisonCount;
        return lhs < rhs;
    };

    peeksort(vec.begin(), vec.end(), countingComp);

    /* In the sorted case comparison complexify should be linear. */
    RC_ASSERT(comparisonCount <= vec.size());
}

} // namespace nix
