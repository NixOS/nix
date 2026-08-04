#pragma once
///@file

#include "nix/util/os-string.hh"

#include <filesystem>
#include <optional>
#include <string_view>

namespace nix {

struct Sink;

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

/**
 * Execute oneself with a given argv[0].
 *
 * Note that unlike getNixBin we don't need to figure out an actual location
 * on disk where the binary resides (at least on systems with /proc/self/exe).
 * The executed binary can even be unlinked from the filesystem and this would
 * still work.
 *
 * This also has the benefit of being able to run multicall binary commands
 * (like nix2-style nix-) without needing the symlinks being located anywhere on
 * the filesystem. We just spoof the argv[0] before execv-ing the current binary.
 */
void runNixBin2(
    std::optional<std::string_view> binaryNameOpt,
    const OsStrings & args,
    bool isInteractive = false,
    std::optional<OsStringMap> environment = std::nullopt,
    Sink * standardOut = nullptr);

std::string runNixBin(std::optional<std::string_view> binaryNameOpt, const OsStrings & args);

} // namespace nix
