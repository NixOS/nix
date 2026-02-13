#include "nix/fetchers/fetch-settings.hh"

namespace nix::fetchers {

Settings::Settings(const nix::Settings & storeSettings)
    : storeSettings(storeSettings)
{
}

} // namespace nix::fetchers
