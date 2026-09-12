#pragma once
///@file

#include "nix/util/processes.hh"

namespace nix::unix {

/**
 * Check `options.redirections` against the constraints documented on
 * `RunOptions::Redirection`, throwing `UsageError` if any is violated.
 *
 * This runs in the parent, deliberately. The child side of `startProgram`
 * applies the duplications in order with no bookkeeping, and on Linux it runs
 * after `vfork` where throwing is not permitted at all — so a caller mistake
 * has to be caught before the fork or it becomes a silently wrong descriptor
 * table in the child.
 */
void validateRedirections(const RunOptions & options);

} // namespace nix::unix
