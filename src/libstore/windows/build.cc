#include "store-api.hh"
#include "build-result.hh"

namespace nix {

void Store::buildPaths(const std::vector<DerivedPath> & reqs, BuildMode buildMode, std::shared_ptr<Store> evalStore)
{
    unsupported("buildPaths");
}

std::vector<KeyedBuildResult> Store::buildPathsWithResults(
    const std::vector<DerivedPath> & reqs,
    BuildMode buildMode,
    std::shared_ptr<Store> evalStore)
{
    unsupported("buildPathsWithResults");
}

BuildResult Store::buildDerivation(const StorePath & drvPath, const BasicDerivation & drv,
    BuildMode buildMode)
{
    unsupported("buildDerivation");
}


void Store::ensurePath(const StorePath & path)
{
    unsupported("ensurePath");
}


void Store::repairPath(const StorePath & path)
{
    unsupported("repairPath");
}

}
