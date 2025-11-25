#include "nix/fetchers/fetch-settings.hh"

namespace nix::fetchers {

Settings::Settings(const nix::Settings & settings)
    : settings(settings)
{
}

} // namespace nix::fetchers
