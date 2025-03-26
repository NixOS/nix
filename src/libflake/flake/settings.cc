#include "flake/settings.hh"
#include "flake/flake-primops.hh"

namespace nix::flake {

Settings::Settings() {}

void Settings::configureEvalSettings(nix::EvalSettings & evalSettings)
{
    evalSettings.addPrimOp(primops::getFlake(*this));
}

} // namespace nix
