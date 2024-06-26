#include "fetch-settings.hh"
#include "config-global.hh"

namespace nix {

FetchSettings::FetchSettings()
{
}

FetchSettings fetchSettings;

static GlobalConfig::Register rFetchSettings(&fetchSettings);

}
