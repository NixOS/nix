#pragma once
///@file

#include <cinttypes>
#include <functional>

namespace nix {

/**
 * An index into a position table (probably `PosTable`).
 *
 * @see PosTable
 */
class PosIdx
{
    friend struct LazyPosAccessors;
    friend class PosTable;
    friend class std::hash<PosIdx>;

private:
    uint32_t id;

    constexpr explicit PosIdx(uint32_t id)
        : id(id)
    {
    }

public:
    constexpr PosIdx()
        : PosIdx(0)
    {
    }

    explicit operator bool() const
    {
        return id > 0;
    }

    auto operator<=>(const PosIdx other) const
    {
        return id <=> other.id;
    }

    bool operator==(const PosIdx other) const
    {
        return id == other.id;
    }

    size_t hash() const noexcept
    {
        return std::hash<uint32_t>{}(id);
    }
};

constexpr inline PosIdx noPos = {};

/**
 * A pair of position indices which together denote a range.
 *
 * The first one must be after the second one.
 */
struct RangeIdxs
{
    PosIdx start;
    PosIdx end;

    bool operator==(const RangeIdxs &) const = default;
    auto operator<=>(const RangeIdxs &) const = default;
};

constexpr inline RangeIdxs noRange = {};

} // namespace nix

namespace std {

template<>
struct hash<nix::PosIdx>
{
    std::size_t operator()(nix::PosIdx pos) const noexcept
    {
        return pos.hash();
    }
};

} // namespace std
