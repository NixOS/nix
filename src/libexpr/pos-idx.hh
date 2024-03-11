#pragma once

#include <cinttypes>

namespace nix {

class PosIdx
{
    friend struct LazyPosAcessors;
    friend class PosTable;

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

    bool operator<(const PosIdx other) const
    {
        return id < other.id;
    }

    bool operator==(const PosIdx other) const
    {
        return id == other.id;
    }

    bool operator!=(const PosIdx other) const
    {
        return id != other.id;
    }
};

inline PosIdx noPos = {};

}
