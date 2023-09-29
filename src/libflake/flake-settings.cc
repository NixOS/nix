#include "flake-settings.hh"

namespace nix {

FlakeSettings::FlakeSettings()
{
}

FlakeSettings flakeSettings;

static GlobalConfig::Register rFlakeSettings(&flakeSettings);

}
