#pragma once
///@file

#include "nix/util/types.hh"

namespace nix {

class AutoRemoveJail
{
    bool del;
public:
    int jid;
    AutoRemoveJail(int jid);
    AutoRemoveJail(const AutoRemoveJail &) = delete;
    AutoRemoveJail & operator=(const AutoRemoveJail &) = delete;

    AutoRemoveJail(AutoRemoveJail && other) noexcept
        : jid(other.jid)
        , del(other.del)
    {
        other.cancel();
    }

    AutoRemoveJail & operator=(AutoRemoveJail && other) noexcept
    {
        jid = other.jid;
        del = other.del;
        other.cancel();
        return *this;
    }

    AutoRemoveJail();
    ~AutoRemoveJail();
    void cancel();
    void reset(int j);
};

} // namespace nix
