#include "nix/flake/settings.hh"
#include "nix/flake/flake-primops.hh"

namespace nix::flake {

Settings::Settings() {}

void Settings::configureEvalSettings(nix::EvalSettings & evalSettings) const
{
    evalSettings.extraPrimOps.emplace_back(primops::getFlake(*this));
    evalSettings.extraPrimOps.emplace_back(primops::parseFlakeRef);
    evalSettings.extraPrimOps.emplace_back(primops::flakeRefToString);
}

} // namespace nix::flake
