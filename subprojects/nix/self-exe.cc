#include "current-process.hh"
#include "file-system.hh"
#include "globals.hh"
#include "self-exe.hh"

namespace nix {

namespace fs {
using namespace std::filesystem;
}

fs::path getNixBin(std::optional<std::string_view> binaryNameOpt)
{
    auto getBinaryName = [&] { return binaryNameOpt ? *binaryNameOpt : "nix"; };

    // If the environment variable is set, use it unconditionally.
    if (auto envOpt = getEnvNonEmpty("NIX_BIN_DIR"))
        return fs::path{*envOpt} / std::string{getBinaryName()};

    // Try OS tricks, if available, to get to the path of this Nix, and
    // see if we can find the right executable next to that.
    if (auto selfOpt = getSelfExe()) {
        fs::path path{*selfOpt};
        if (binaryNameOpt)
            path = path.parent_path() / std::string{*binaryNameOpt};
        if (fs::exists(path))
            return path;
    }

    // If `nix` exists at the hardcoded fallback path, use it.
    {
        auto path = fs::path{NIX_BIN_DIR} / std::string{getBinaryName()};
        if (fs::exists(path))
            return path;
    }

    // return just the name, hoping the exe is on the `PATH`
    return getBinaryName();
}

}
