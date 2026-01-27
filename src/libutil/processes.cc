#include "nix/util/processes.hh"

namespace nix {

Pid & Pid::operator=(Pid && other) noexcept
{
    swap(*this, other);
    return *this;
}

} // namespace nix
