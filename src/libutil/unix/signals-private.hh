#pragma once

#include <signal.h>

namespace nix {
namespace unix {

extern sigset_t savedSignalMask;
extern bool savedSignalMaskIsSet;

/**
 * Discard inherited registrations, which may refer to vanished threads or
 * unusable parent state. Call only in a fork/clone child, before starting any
 * threads or using signal callbacks; this is not a concurrent reset operation.
 */
void resetSignalCallbacksAfterFork();

} // namespace unix
} // namespace nix
