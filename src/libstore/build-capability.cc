#include "build-capability.hh"
#include <algorithm>

namespace nix {

bool
BuildCapability::canBuild(const Schedulable & schedulable) const
{
    return schedulable.getSystem() == system
        && std::includes(
            supportedFeatures.begin(), supportedFeatures.end(),
            schedulable.getRequiredFeatures().begin(), schedulable.getRequiredFeatures().end()
            )
        && std::includes(
            schedulable.getRequiredFeatures().begin(), schedulable.getRequiredFeatures().end(),
            mandatoryFeatures.begin(), mandatoryFeatures.end()
            );
}

} // namespace nix
