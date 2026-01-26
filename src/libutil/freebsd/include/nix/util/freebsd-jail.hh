#pragma once
///@file

#include "nix/util/types.hh"

namespace nix {

class AutoRemoveJail
{
    static constexpr int INVALID_JAIL = -1;
    int jid = INVALID_JAIL;
public:
    AutoRemoveJail();
    AutoRemoveJail(int jid);
    AutoRemoveJail(const AutoRemoveJail &) = delete;
    AutoRemoveJail & operator=(const AutoRemoveJail &) = delete;

    AutoRemoveJail(AutoRemoveJail && other) noexcept
        : jid(other.jid)
    {
        other.cancel();
    }

    AutoRemoveJail & operator=(AutoRemoveJail && other) noexcept
    {
        swap(*this, other);
        return *this;
    }

    friend void swap(AutoRemoveJail & lhs, AutoRemoveJail & rhs) noexcept
    {
        using std::swap;
        swap(lhs.jid, rhs.jid);
    }

    operator int() const
    {
        return jid;
    }

    ~AutoRemoveJail();

    /**
     * remove the jail that path points to, and cancel this
     * `AutoRemoveJail`, so unmounting is not attempted a second time by the
     * destructor.
     *
     * The destructor calls this ignoring any exception.
     */
    void remove();

    /**
     * Cancel the jail removal.
     */
    void cancel();
};

} // namespace nix
