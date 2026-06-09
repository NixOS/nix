#pragma once
///@file

#include <cstdint>
#include <cstdlib>
#include <vector>
#include <limits>
#include <atomic>
#include <ranges>

#include "nix/util/error.hh"

namespace nix {

/**
 * Provides an indexable container like vector<> with memory overhead
 * guarantees like list<> by allocating storage in chunks of ChunkSize
 * elements instead of using a contiguous memory allocation like vector<>
 * does. Not using a single vector that is resized reduces memory overhead
 * on large data sets by on average (growth factor)/2, mostly
 * eliminates copies within the vector during resizing, and provides stable
 * references to its elements.
 *
 * Thread-safe to append and read concurrently. Caps the maximum number of chunks
 * to MaxChunks for ease of implementation.
 */
template<typename T, size_t ChunkSize, size_t MaxChunks>
class ChunkedVector
{
    static_assert(alignof(T) <= __STDCPP_DEFAULT_NEW_ALIGNMENT__, "ChunkedVector does not support over-aligned types");

private:
    /* Put on a separate cache line to avoid false sharing. */
    alignas(std::hardware_destructive_interference_size) std::atomic<uint32_t> size_ = 0;

    using ChunksArray = std::array<std::atomic<T *>, MaxChunks>;

    std::unique_ptr<ChunksArray> chunks = std::make_unique<ChunksArray>();

    [[gnu::noinline]]
    T * allocChunk()
    {
        return static_cast<T *>(::operator new(ChunkSize * sizeof(T)));
    }

    /**
     * Keep this out of the ::add hot path
     */
    [[gnu::noinline]]
    T * ensureChunk(size_t chunkIdx)
    {
        if (chunkIdx >= MaxChunks)
            unreachable();

        /* Want synchronises-with relation with the CAS. */
        auto * p = (*chunks)[chunkIdx].load(std::memory_order_acquire);
        if (p) [[likely]]
            return p;

        auto * newChunk = allocChunk();
        T * expected = nullptr;
        /* Try to allocate the chunk ourselves. If CAS fails then somebody beat
           us to it. Release semantics for the winner and acquire semantics for
           the loser. */
        if ((*chunks)[chunkIdx].compare_exchange_strong(
                expected, newChunk, /*success=*/std::memory_order_release, /*failure=*/std::memory_order_acquire))
            return newChunk; /* We succeeded. */

        /* Somebody beat us to it. */
        ::operator delete(newChunk);
        assert(expected);
        return expected;
    }

    std::size_t numActiveChunks() const noexcept
    {
        return (size_.load(std::memory_order_relaxed) + ChunkSize - 1) / ChunkSize;
    }

public:
    ChunkedVector() = default;

    ChunkedVector(ChunkedVector &&) = delete;
    ChunkedVector(const ChunkedVector &) = delete;
    ChunkedVector & operator=(ChunkedVector &&) = delete;
    ChunkedVector & operator=(const ChunkedVector &) = delete;

    ~ChunkedVector()
    {
        auto len = size();
        for (auto & chunkPtr : std::ranges::views::take(*chunks, numActiveChunks())) {
            auto * chunk = chunkPtr.load(std::memory_order_relaxed);
            auto count = std::min<decltype(len)>(len, ChunkSize);
            if constexpr (!std::is_trivially_destructible_v<T>) {
                std::destroy_n(chunk, count);
            }
            ::operator delete(chunk);
            len -= count;
        }
    }

    /**
     * Get the current size.
     */
    uint32_t size() const noexcept
    {
        return size_.load(std::memory_order_relaxed);
    }

    template<typename... Args>
    std::pair<T &, uint32_t> add(Args &&... args)
    {
        /* Get some unique index. Doesn't need any synchronises-with relation. */
        const auto idx = size_.fetch_add(1, std::memory_order_relaxed);
        auto * chunk = ensureChunk(idx / ChunkSize);
        auto * p = new (&chunk[idx % ChunkSize]) T(std::forward<Args>(args)...);
        return {*p, idx};
    }

    /**
     * Unchecked subscript operator.
     * @pre add must have been called at least idx + 1 times.
     * @throws nothing
     */
    const T & operator[](uint32_t idx) const & noexcept
    {
        /* Synchronises-with relation with add(). */
        auto * chunk = (*chunks)[idx / ChunkSize].load(std::memory_order_acquire);
        return chunk[idx % ChunkSize];
    }

    /**
     * Iterate over all elements. Do not use when there might be concurrent writers.
     */
    template<typename Fn>
    void forEach(Fn fn) const
    {
        auto len = size();
        for (auto & chunkPtr : std::ranges::views::take(*chunks, numActiveChunks())) {
            auto * chunk = chunkPtr.load(std::memory_order_relaxed);
            auto count = std::min<decltype(len)>(len, ChunkSize);
            std::for_each_n(chunk, count, fn);
            len -= count;
        }
    }
};
} // namespace nix
