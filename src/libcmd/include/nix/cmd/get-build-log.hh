#pragma once
///@file

#include "nix/util/ref.hh"

#include <string>
#include <string_view>

namespace nix {

class StorePath;

class Store;

/**
 * Fetch the build log for a store path, searching the store and its
 * substituters.
 *
 * @param store The store to search (and its substituters).
 * @param path The store path to get the build log for.
 * @param what A description of what we're fetching the log for (used in messages).
 * @return The build log content.
 * @throws Error if the build log is not available.
 */
std::string fetchBuildLog(ref<Store> store, const StorePath & path, std::string_view what);

} // namespace nix
