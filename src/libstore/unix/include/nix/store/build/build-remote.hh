#pragma once
///@file

#include "nix/store/store-api.hh"
#include "nix/util/file-descriptor.hh"
#include "nix/util/serialise.hh"

namespace nix {

/**
 * The build hook: answer requests from `from` on `toParent` and run
 * an accepted build on a machine from `builders`. Returns at EOF or
 * after one accepted build.
 *
 * @param maxBuildJobs the parent's `max-jobs`.
 * @param sshErrorFd ssh's stderr, read when a connection fails.
 */
void serveBuildHook(
    ref<Store> store, unsigned int maxBuildJobs, Source & from, Descriptor toParent, Descriptor sshErrorFd);

} // namespace nix
