#include "flake-settings.hh"
#include "config-global.hh"

namespace nix {

FlakeSettings::FlakeSettings()
{
}

FlakeSettings flakeSettings;

static GlobalConfig::Register rFlakeSettings(&flakeSettings);

}
