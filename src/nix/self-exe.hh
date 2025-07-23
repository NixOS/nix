#pragma once
///@file

#include <filesystem>
#include <optional>
#include <string_view>

namespace nix {

/**
 * Get a path to the given Nix binary.
 *
 * Normally, nix is installed according to `NIX_BIN_DIR`, which is set
 * at compile time, but can be overridden.
 *
 * However, it may not have been installed at all. For example, if it's
 * a static build, there's a good chance that it has been moved out of
 * its installation directory. That makes `NIX_BIN_DIR` useless.
 * Instead, we'll query the OS for the path to the current executable,
 * using `getSelfExe()`.
 *
 * As a last resort, we rely on `PATH`. Hopefully we find a `nix` there
 * that's compatible. If you're porting Nix to a new platform, that
 * might be good enough for a while, but you'll want to improve
 * `getSelfExe()` to work on your platform.
 *
 * @param binary_name the exact binary name we're looking up. Might be
 * `nix-*` instead of `nix` for the legacy CLI commands. Optional to use
 * current binary name.
 */
std::filesystem::path getNixBin(std::optional<std::string_view> binary_name = {});

} // namespace nix
