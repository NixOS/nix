#pragma once

#include <signal.h>

namespace nix {
namespace unix {

extern sigset_t savedSignalMask;
extern bool savedSignalMaskIsSet;

} // namespace unix
} // namespace nix
