#pragma once

#include <cinttypes>
#include <numeric>
#include <vector>

#include "pos-idx.hh"
#include "position.hh"
#include "sync.hh"

namespace nix {

class PosTable
{
public:
    class Origin
    {
        friend PosTable;
    private:
        uint32_t offset;

        Origin(Pos::Origin origin, uint32_t offset, size_t size):
            offset(offset), origin(origin), size(size)
        {}

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

    mutable Sync<std::map<uint32_t, Lines>> lines;

    // FIXME: this could be made lock-free (at least for access) if we
    // have a data structure where pointers to existing positions are
    // never invalidated.
    struct State
    {
        std::map<uint32_t, Origin> origins;
    };

    SharedSync<State> state_;

public:
    PosTable()
    { }

    const Origin * resolve(PosIdx p) const
    {
        if (p.id == 0)
            return nullptr;

        auto state(state_.read());
        const auto idx = p.id - 1;
        /* We want the last key <= idx, so we'll take prev(first key >
           idx). This is guaranteed to never rewind origin.begin
           because the first key is always 0. */
        const auto pastOrigin = state->origins.upper_bound(idx);
        return &std::prev(pastOrigin)->second;
    }

public:
    Origin addOrigin(Pos::Origin origin, size_t size)
    {
        auto state(state_.lock());
        uint32_t offset = 0;
        if (auto it = state->origins.rbegin(); it != state->origins.rend())
            offset = it->first + it->second.size;
        // +1 because all PosIdx are offset by 1 to begin with, and
        // another +1 to ensure that all origins can point to EOF, eg
        // on (invalid) empty inputs.
        if (2 + offset + size < offset)
            return Origin{origin, offset, 0};
        return state->origins.emplace(offset, Origin{origin, offset, size}).first->second;
    }

    PosIdx add(const Origin & origin, size_t offset)
    {
        if (offset > origin.size)
            return PosIdx();
        return PosIdx(1 + origin.offset + offset);
    }

    Pos operator[](PosIdx p) const;

    Pos::Origin originOf(PosIdx p) const
    {
        if (auto o = resolve(p))
            return o->origin;
        return std::monostate{};
    }
};

}
