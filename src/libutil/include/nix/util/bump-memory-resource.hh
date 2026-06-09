#pragma once
///@file

#include <atomic>
#include <memory_resource>

namespace nix {

/**
 * Bump allocator that tries to overcommit memory and does thread-safe bump allocation.
 * Deallocates memory on destruction.
 */
class BumpMemoryResource : public std::pmr::memory_resource
{
private:
    void * base = nullptr;
    std::size_t capacity = 0;
    std::pmr::memory_resource * upstreamResource;

    /* Put on a separate cache line to avoid false sharing. */
    alignas(std::hardware_destructive_interference_size) std::atomic<std::size_t> offset{};

protected:
    void * do_allocate(std::size_t bytes, std::size_t alignment) override;

    void do_deallocate(void * p, std::size_t bytes, std::size_t alignment) override;

    bool do_is_equal(const std::pmr::memory_resource & other) const noexcept override
    {
        return this == &other;
    }

public:
    /* Note that overcommit isn't done with 32 bit builds anyway. Just to avoid overflows
       in the constructor. 8GiB should be plenty enough with 64 bit address space to never
       fall back to the upstream resource and is very cheap to reserve up-front. */
    static constexpr std::size_t defaultReserveSize =
        sizeof(void *) >= 8 ? std::size_t(8) << 30 : std::size_t(64) << 20;

    explicit BumpMemoryResource(
        std::size_t reserveSize = defaultReserveSize,
        std::pmr::memory_resource * upstream = std::pmr::new_delete_resource());

    BumpMemoryResource(BumpMemoryResource &&) = delete;
    BumpMemoryResource(const BumpMemoryResource &) = delete;
    BumpMemoryResource & operator=(BumpMemoryResource &&) = delete;
    BumpMemoryResource & operator=(const BumpMemoryResource &) = delete;

    ~BumpMemoryResource() override;
};

} // namespace nix
