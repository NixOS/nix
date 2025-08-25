#include "nix/fetchers/fetch-settings.hh"
#include "nix/util/config-global.hh"

namespace nix::fetchers {

Settings::Settings() {}

} // namespace nix::fetchers

namespace nix {

fetchers::Settings fetchSettings;

static GlobalConfig::Register rFetchSettings(&fetchSettings);

} // namespace nix
