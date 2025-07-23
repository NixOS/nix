#include "nix/util/current-process.hh"
#include "nix/util/file-system.hh"
#include "nix/store/globals.hh"

#include "self-exe.hh"
#include "cli-config-private.hh"

namespace nix {

std::filesystem::path getNixBin(std::optional<std::string_view> binaryNameOpt)
{
    auto getBinaryName = [&] { return binaryNameOpt ? *binaryNameOpt : "nix"; };

    // If the environment variable is set, use it unconditionally.
    if (auto envOpt = getEnvNonEmpty("NIX_BIN_DIR"))
        return std::filesystem::path{*envOpt} / std::string{getBinaryName()};

    // Try OS tricks, if available, to get to the path of this Nix, and
    // see if we can find the right executable next to that.
    if (auto selfOpt = getSelfExe()) {
        std::filesystem::path path{*selfOpt};
        if (binaryNameOpt)
            path = path.parent_path() / std::string{*binaryNameOpt};
        if (std::filesystem::exists(path))
            return path;
    }

    // If `nix` exists at the hardcoded fallback path, use it.
    {
        auto path = std::filesystem::path{NIX_BIN_DIR} / std::string{getBinaryName()};
        if (std::filesystem::exists(path))
            return path;
    }

    // return just the name, hoping the exe is on the `PATH`
    return getBinaryName();
}

} // namespace nix
