#pragma once
///@file

#include <filesystem>
#include <vector>

namespace nix {

/**
 * The directory where system configuration files are stored.
 *
 * This is needed very early during initialization, before a main
 * `Settings` object can be constructed.
 */
const std::filesystem::path & nixConfDir();

/**
 * The path to the system configuration file (`nix.conf`).
 */
static inline std::filesystem::path nixConfFile()
{
    return nixConfDir() / "nix.conf";
}

/**
 * A list of user configuration files to load.
 *
 * This is needed very early during initialization, before a main
 * `Settings` object can be constructed.
 */
const std::vector<std::filesystem::path> & nixUserConfFiles();

} // namespace nix
