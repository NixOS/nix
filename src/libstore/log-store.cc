#include "nix/store/log-store.hh"

namespace nix {

std::optional<std::string> LogStore::getBuildLog(const StorePath & path) {
    auto maybePath = getBuildDerivationPath(path);
    if (!maybePath)
        return std::nullopt;
    return getBuildLogExact(maybePath.value());
}

}
