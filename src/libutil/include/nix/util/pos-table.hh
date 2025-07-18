#pragma once
///@file

#include <cstdint>
#include <vector>

#include "nix/util/pos-idx.hh"
#include "nix/util/position.hh"
#include "nix/util/sync.hh"

namespace nix {

class PosTable
{
public:
    class Origin
    {
        friend PosTable;
    private:
        uint32_t offset;

        Origin(Pos::Origin origin, uint32_t offset, size_t size)
            : offset(offset)
            , origin(origin)
            , size(size)
        {
        }

    public:
        const Pos::Origin origin;
        const size_t size;

        uint32_t offsetOf(PosIdx p) const
        {
            return p.id - 1 - offset;
        }
    };

private:
    using Lines = std::vector<uint32_t>;

    std::map<uint32_t, Origin> origins;
    mutable Sync<std::map<uint32_t, Lines>> lines;

    const Origin * resolve(PosIdx p) const
    {
        if (p.id == 0)
            return nullptr;

        const auto idx = p.id - 1;
        /* we want the last key <= idx, so we'll take prev(first key > idx).
            this is guaranteed to never rewind origin.begin because the first
            key is always 0. */
        const auto pastOrigin = origins.upper_bound(idx);
        return &std::prev(pastOrigin)->second;
    }

public:
    Origin addOrigin(Pos::Origin origin, size_t size)
    {
        uint32_t offset = 0;
        if (auto it = origins.rbegin(); it != origins.rend())
            offset = it->first + it->second.size;
        // +1 because all PosIdx are offset by 1 to begin with, and
        // another +1 to ensure that all origins can point to EOF, eg
        // on (invalid) empty inputs.
        if (2 + offset + size < offset)
            return Origin{origin, offset, 0};
        return origins.emplace(offset, Origin{origin, offset, size}).first->second;
    }

    PosIdx add(const Origin & origin, size_t offset)
    {
        if (offset > origin.size)
            return PosIdx();
        return PosIdx(1 + origin.offset + offset);
    }

    /**
     * Convert a byte-offset PosIdx into a Pos with line/column information.
     *
     * @param p Byte offset into the virtual concatenation of all parsed contents
     * @return Position
     *
     * @warning Very expensive to call, as this has to read the entire source
     * into memory each time. Call this only if absolutely necessary. Prefer
     * to keep PosIdx around instead of needlessly converting it into Pos by
     * using this lookup method.
     */
    Pos operator[](PosIdx p) const;

    Pos::Origin originOf(PosIdx p) const
    {
        if (auto o = resolve(p))
            return o->origin;
        return std::monostate{};
    }
};

} // namespace nix
