#pragma once
/**
 * @file
 *
 * macOS-only support for materializing profile-installed `.app` bundles in a
 * Spotlight-indexable location.
 */

#include <filesystem>

namespace nix::darwin {

/** Best-effort; never lets failures abort a profile switch. */
void syncProfileAppBundles(const std::filesystem::path & profile) noexcept;

} // namespace nix::darwin
