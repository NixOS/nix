#pragma once
///@file

#include <cinttypes>
#include <functional>

namespace nix {

class PosIdx
{
    friend struct LazyPosAccessors;
    friend class PosTable;
    friend class std::hash<PosIdx>;

private:
    uint32_t id;

public:
    explicit PosIdx(uint32_t id)
        : id(id)
    {
    }

    PosIdx()
        : id(0)
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

    uint32_t get() const
    {
        return id;
    }
};

inline PosIdx noPos = {};

}

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
