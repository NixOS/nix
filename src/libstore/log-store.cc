#include "nix/store/log-store.hh"
#include "nix/store/path.hh"

#include <optional>
#include <string>

namespace nix {

void LogStore::anchor() {}

std::optional<std::string> LogStore::getBuildLog(const StorePath & path)
{
    auto maybePath = getBuildDerivationPath(path);
    if (!maybePath)
        return std::nullopt;
    return getBuildLogExact(maybePath.value());
}

} // namespace nix
