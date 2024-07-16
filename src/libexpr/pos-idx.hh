#pragma once

#include <cinttypes>

#include "util.hh"

namespace nix {

class PosIdx
{
    friend struct LazyPosAcessors;
    friend class PosTable;
    friend class std::hash<PosIdx>;

private:
    uint32_t id;

    explicit PosIdx(uint32_t id)
        : id(id)
    {
    }

public:
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
        size_t h = 854125;
        hash_combine(h, id);
        return h;
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
