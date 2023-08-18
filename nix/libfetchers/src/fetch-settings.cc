#include "fetch-settings.hh"

namespace nix {

FetchSettings::FetchSettings()
{
}

FetchSettings fetchSettings;

static GlobalConfig::Register rFetchSettings(&fetchSettings);

}
