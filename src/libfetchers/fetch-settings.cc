#include "nix/fetchers/fetch-settings.hh"

namespace nix::fetchers {

Settings::Settings(SecretSpecSettings & secretSpecSettings)
    : secretSpecSettings(secretSpecSettings)
{
}

void Settings::anchor() {}

std::string Settings::getSecretSpecAccessToken(const std::string & name) const
{
    return secretSpecSettings.getInlineSecret(name);
}

} // namespace nix::fetchers
