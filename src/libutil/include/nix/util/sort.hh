#pragma once

#include <algorithm>
#include <iterator>
#include <concepts>
#include <vector>
#include <type_traits>
#include <functional>

/**
 * @file
 *
 * In-house implementation of sorting algorithms. Used for cases when several properties
 * need to be upheld regardless of the stdlib implementation of std::sort or
 * std::stable_sort.
 *
 * PeekSort implementation is adapted from reference implementation
 * https://github.com/sebawild/powersort licensed under the MIT License.
 *
 */

/* PeekSort attribution:
 *
 *  MIT License
 *
 *  Copyright (c) 2022 Sebastian Wild
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files (the "Software"), to deal
 *  in the Software without restriction, including without limitation the rights
 *  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 *  copies of the Software, and to permit persons to whom the Software is
 *  furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in all
 *  copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 *  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 *  SOFTWARE.
 */

namespace nix {

/**
 * Merge sorted runs [begin, middle) with [middle, end) in-place [begin, end).
 * Uses a temporary working buffer by first copying [begin, end) to it.
 *
 * @param begin Start of the first subrange to be sorted.
 * @param middle End of the first sorted subrange and the start of the second.
 * @param end End of the second sorted subrange.
 * @param workingBegin Start of the working buffer.
 * @param comp Comparator implementing an operator()(const ValueType& lhs, const ValueType& rhs).
 *
 * @pre workingBegin buffer must have at least std::distance(begin, end) elements.
 *
 * @note We can't use std::inplace_merge or std::merge, because their behavior
 * is undefined if the comparator is not strict weak ordering.
 */
template<
    std::forward_iterator Iter,
    std::random_access_iterator BufIter,
    typename Comparator = std::less<std::iter_value_t<Iter>>>
void mergeSortedRunsInPlace(Iter begin, Iter middle, Iter end, BufIter workingBegin, Comparator comp = {})
{
    const BufIter workingMiddle = std::move(begin, middle, workingBegin);
    const BufIter workingEnd = std::move(middle, end, workingMiddle);

    Iter output = begin;
    BufIter workingLeft = workingBegin;
    BufIter workingRight = workingMiddle;

    while (workingLeft != workingMiddle && workingRight != workingEnd) {
        /* Note the inversion here !comp(...., ....). This is required for the merge to be stable.
           If a == b where a if from the left part and b is the the right, then we have to pick
           a. */
        *output++ = !comp(*workingRight, *workingLeft) ? std::move(*workingLeft++) : std::move(*workingRight++);
    }

    std::move(workingLeft, workingMiddle, output);
    std::move(workingRight, workingEnd, output);
}

/**
 * Simple insertion sort.
 *
 * Does not require that the std::iter_value_t<Iter> is copyable.
 *
 * @param begin Start of the range to sort.
 * @param end End of the range to sort.
 * @comp Comparator the defines the ordering. Order of elements if the comp is not strict weak ordering
 * is not specified.
 * @throws Nothing.
 *
 * Note on exception safety: this function provides weak exception safety
 * guarantees. To elaborate: if the comparator throws or move assignment
 * throws (value type is not nothrow_move_assignable) then the range is left in
 * a consistent, but unspecified state.
 *
 * @note This can't be implemented in terms of binary search if the strict weak ordering
 * needs to be handled in a well-defined but unspecified manner.
 */
template<std::bidirectional_iterator Iter, typename Comparator = std::less<std::iter_value_t<Iter>>>
void insertionsort(Iter begin, Iter end, Comparator comp = {})
{
    if (begin == end)
        return;
    for (Iter current = std::next(begin); current != end; ++current) {
        for (Iter insertionPoint = current;
             insertionPoint != begin && comp(*insertionPoint, *std::prev(insertionPoint));
             --insertionPoint) {
            std::swap(*insertionPoint, *std::prev(insertionPoint));
        }
    }
}

/**
 * Find maximal i <= end such that [begin, i) is strictly decreasing according
 * to the specified comparator.
 */
template<std::forward_iterator Iter, typename Comparator = std::less<std::iter_value_t<Iter>>>
Iter strictlyDecreasingPrefix(Iter begin, Iter end, Comparator && comp = {})
{
    if (begin == end)
        return begin;
    while (std::next(begin) != end && /* *std::next(begin) < begin */
           comp(*std::next(begin), *begin))
        ++begin;
    return std::next(begin);
}

/**
 * Find minimal i >= start such that [i, end) is strictly decreasing according
 * to the specified comparator.
 */
template<std::bidirectional_iterator Iter, typename Comparator = std::less<std::iter_value_t<Iter>>>
Iter strictlyDecreasingSuffix(Iter begin, Iter end, Comparator && comp = {})
{
    if (begin == end)
        return end;
    while (std::prev(end) > begin && /* *std::prev(end) < *std::prev(end, 2) */
           comp(*std::prev(end), *std::prev(end, 2)))
        --end;
    return std::prev(end);
}

/**
 * Find maximal i <= end such that [begin, i) is weakly increasing according
 * to the specified comparator.
 */
template<std::bidirectional_iterator Iter, typename Comparator = std::less<std::iter_value_t<Iter>>>
Iter weaklyIncreasingPrefix(Iter begin, Iter end, Comparator && comp = {})
{
    return strictlyDecreasingPrefix(begin, end, std::not_fn(std::forward<Comparator>(comp)));
}

/**
 * Find minimal i >= start such that [i, end) is weakly increasing according
 * to the specified comparator.
 */
template<std::bidirectional_iterator Iter, typename Comparator = std::less<std::iter_value_t<Iter>>>
Iter weaklyIncreasingSuffix(Iter begin, Iter end, Comparator && comp = {})
{
    return strictlyDecreasingSuffix(begin, end, std::not_fn(std::forward<Comparator>(comp)));
}

/**
 * Peeksort stable sorting algorithm. Sorts elements in-place.
 * Allocates additional memory as needed.
 *
 * @details
 * PeekSort is a stable, near-optimal natural mergesort. Most importantly, like any
 * other mergesort it upholds the "Ord safety" property. Meaning that even for
 * comparator predicates that don't satisfy strict weak ordering it can't result
 * in infinite loops/out of bounds memory accesses or other undefined behavior.
 *
 * As a quick reminder, strict weak ordering relation operator< must satisfy
 * the following properties. Keep in mind that in C++ an equvalence relation
 * is specified in terms of operator< like so: a ~ b iff !(a < b) && !(b < a).
 *
 * 1. a < a === false - relation is irreflexive
 * 2. a < b, b < c => a < c - transitivity
 * 3. a ~ b, a ~ b, b ~ c => a ~ c, transitivity of equivalence
 *
 * @see https://www.wild-inter.net/publications/munro-wild-2018
 * @see https://github.com/Voultapher/sort-research-rs/blob/main/writeup/sort_safety/text.md#property-analysis
 *
 * The order of elements when comp is not strict weak ordering is not specified, but
 * is not undefined. The output is always some permutation of the input, regardless
 * of the comparator provided.
 * Relying on ordering in such cases is erroneous, but this implementation
 * will happily accept broken comparators and will not crash.
 *
 * @param begin Start of the range to be sorted.
 * @param end End of the range to be sorted.
 * @comp comp Comparator implementing an operator()(const ValueType& lhs, const ValueType& rhs).
 *
 * @throws std::bad_alloc if the temporary buffer can't be allocated.
 *
 * @return Nothing.
 *
 * Note on exception safety: this function provides weak exception safety
 * guarantees. To elaborate: if the comparator throws or move assignment
 * throws (value type is not nothrow_move_assignable) then the range is left in
 * a consistent, but unspecified state.
 *
 */
template<std::random_access_iterator Iter, typename Comparator = std::less<std::iter_value_t<Iter>>>
/* ValueType must be default constructible to create the temporary buffer */
    requires std::is_default_constructible_v<std::iter_value_t<Iter>>
void peeksort(Iter begin, Iter end, Comparator comp = {})
{
    auto length = std::distance(begin, end);

    /* Special-case very simple inputs. This is identical to how libc++ does it. */
    switch (length) {
    case 0:
        [[fallthrough]];
    case 1:
        return;
    case 2:
        if (comp(*--end, *begin)) /* [a, b], b < a */
            std::swap(*begin, *end);
        return;
    }

    using ValueType = std::iter_value_t<Iter>;
    auto workingBuffer = std::vector<ValueType>(length);

    /*
     * sorts [begin, end), assuming that [begin, leftRunEnd) and
     * [rightRunBegin, end) are sorted.
     * Modified implementation from:
     * https://github.com/sebawild/powersort/blob/1d078b6be9023e134c4f8f6de88e2406dc681e89/src/sorts/peeksort.h
     */
    auto peeksortImpl = [&workingBuffer,
                         &comp](auto & peeksortImpl, Iter begin, Iter end, Iter leftRunEnd, Iter rightRunBegin) {
        if (leftRunEnd == end || rightRunBegin == begin)
            return;

        /* Dispatch to simpler insertion sort implementation for smaller cases
           Cut-off limit is the same as in libstdc++
           https://github.com/gcc-mirror/gcc/blob/d9375e490072d1aae73a93949aa158fcd2a27018/libstdc%2B%2B-v3/include/bits/stl_algo.h#L4977
         */
        static constexpr std::size_t insertionsortThreshold = 16;
        size_t length = std::distance(begin, end);
        if (length <= insertionsortThreshold)
            return insertionsort(begin, end, comp);

        Iter middle = std::next(begin, (length / 2)); /* Middle split between m and m - 1 */

        if (middle <= leftRunEnd) {
            /* |XXXXXXXX|XX     X| */
            peeksortImpl(peeksortImpl, leftRunEnd, end, std::next(leftRunEnd), rightRunBegin);
            mergeSortedRunsInPlace(begin, leftRunEnd, end, workingBuffer.begin(), comp);
            return;
        } else if (middle >= rightRunBegin) {
            /* |XX     X|XXXXXXXX| */
            peeksortImpl(peeksortImpl, begin, rightRunBegin, leftRunEnd, std::prev(rightRunBegin));
            mergeSortedRunsInPlace(begin, rightRunBegin, end, workingBuffer.begin(), comp);
            return;
        }

        /* Find middle run, i.e., run containing m - 1 */
        Iter i, j;

        if (!comp(*middle, *std::prev(middle)) /* *std::prev(middle) <= *middle */) {
            i = weaklyIncreasingSuffix(leftRunEnd, middle, comp);
            j = weaklyIncreasingPrefix(std::prev(middle), rightRunBegin, comp);
        } else {
            i = strictlyDecreasingSuffix(leftRunEnd, middle, comp);
            j = strictlyDecreasingPrefix(std::prev(middle), rightRunBegin, comp);
            std::reverse(i, j);
        }

        if (i == begin && j == end)
            return; /* single run */

        if (middle - i < j - middle) {
            /* |XX     x|xxxx   X| */
            peeksortImpl(peeksortImpl, begin, i, leftRunEnd, std::prev(i));
            peeksortImpl(peeksortImpl, i, end, j, rightRunBegin);
            mergeSortedRunsInPlace(begin, i, end, workingBuffer.begin(), comp);
        } else {
            /* |XX   xxx|x      X| */
            peeksortImpl(peeksortImpl, begin, j, leftRunEnd, i);
            peeksortImpl(peeksortImpl, j, end, std::next(j), rightRunBegin);
            mergeSortedRunsInPlace(begin, j, end, workingBuffer.begin(), comp);
        }
    };

    peeksortImpl(peeksortImpl, begin, end, /*leftRunEnd=*/begin, /*rightRunBegin=*/end);
}

} // namespace nix
