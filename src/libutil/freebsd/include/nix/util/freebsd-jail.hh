#pragma once
///@file

#include "nix/util/types.hh"

namespace nix {

class AutoRemoveJail
{
    int jid;
    bool del;
public:
    AutoRemoveJail(int jid);
    AutoRemoveJail();
    ~AutoRemoveJail();
    void cancel();
    void reset(int j);
};

} // namespace nix
