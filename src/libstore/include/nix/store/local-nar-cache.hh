#pragma once
///@file

#include "nix/util/nar-cache.hh"

#include <filesystem>
#include <memory>

namespace nix {

/**
 * Create a NAR cache with local disk storage.
 *
 * Uses file locks to ensure only one process downloads a NAR at a time.
 *
 * @param cacheDir Directory to store cached NAR files
 */
std::unique_ptr<NarCache> makeLocalNarCache(std::filesystem::path cacheDir);

} // namespace nix
